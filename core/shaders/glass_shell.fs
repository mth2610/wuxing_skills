#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec3 shieldViewDir;

uniform vec4 u_bodyColor;
uniform vec4 u_rimColor;
uniform float u_opacity;
uniform float u_rimStrength;
uniform float u_bodyOpacity;
uniform float u_emissionGain;
uniform int u_emissionOnly;
uniform int u_wallPass;
uniform vec3 u_lightDirView;

/* One lookup: R=hexagon, G=Perlin-like scrolling noise, B=soft mask. */
uniform sampler2D u_packedTex;
uniform int u_hasPacked;
uniform sampler2D u_flowTex;
uniform int u_hasFlow;
uniform sampler2D u_matcapTex;
uniform int u_hasMatcap;
uniform sampler2D u_sceneTex;
uniform int u_hasScene;
uniform float u_noiseScale;
uniform float u_noiseSpeed;
uniform float u_flowStrength;
uniform float u_flowSpeed;
uniform float u_parallaxDepth;
uniform float u_innerDepth;

/* Optional half-resolution depth path; disabled by default on low-end GPUs. */
uniform sampler2D u_depthTex;
uniform int u_hasDepth;
uniform float u_depthEnabled;
uniform float u_depthLod;
uniform float u_contactStrength;
uniform vec4 u_contactColor;
uniform float u_contactThickness;
uniform float u_baseAlpha;
uniform float u_fresnelAlpha;
uniform float u_contactAlpha;

uniform vec3 u_impactView;
uniform float u_impactAge;
uniform float u_rippleFrequency;
uniform float u_rippleSpeed;

float shieldPow4(float x) {
    float x2 = x * x;
    return x2 * x2;
}

float depthContact(vec2 uv) {
    if (u_hasDepth == 0 || u_depthEnabled < 0.5) return 0.0;
    float sceneDepth = texture(u_depthTex, uv).r;
    // SAME MEASURE ON BOTH SIDES. depth_copy.fs stores the standard perspective
    // linearisation, which is distance along the VIEW AXIS; `length(fragPosition)` is
    // RADIAL distance from the eye. Off-axis the radial value is always the larger of
    // the two, so `gap` went negative across most of the shell and this function
    // returned 0 everywhere — the ground-contact term has been dead, which is why
    // toggling `shield_shell_depth_enabled` changed exactly 0.000% of pixels.
    // Immediate-mode attributes arrive in view space (rlvk `imm_normal`), camera down
    // -Z, so the axial distance of a visible fragment is -fragPosition.z.
    float fragDepth = max(-fragPosition.z, 0.0001);
    float gap = sceneDepth - fragDepth;
    // A SCREEN-SPACE WIDTH FLOOR WAS TRIED HERE AND REMOVED. `max(u_contactThickness,
    // fwidth(gap) * 5.0)` looks like the obvious cure for "the rear ground line has no
    // band", and it is not: the band was missing because the depth SNAPSHOT was written
    // to the wrong rows of its target for any region smaller than the full frame (fixed
    // in ScreenDistort_SnapshotDepth), not because 0.35 m was too thin. With the snapshot
    // aligned the floor changes 104 of 921600 pixels by more than 2/255 — nothing — while
    // costing two derivatives and lighting up the silhouette, where fwidth explodes and a
    // 30 m gap still scores as "touching". Left as a note so it is not re-derived.
    if (gap <= 0.0) return 0.0;
    // PEAKED, NOT PLATEAUED. This was `1.0 - smoothstep(0, thickness, gap)`, and
    // smoothstep has ZERO DERIVATIVE at its lower edge: the band came out flat-topped for
    // the first ~15% of its width. Measured on the ground line, that flat top was 13 px
    // wide — and inside it the R channel is pinned at 1.0, so there is no luminance
    // gradient left and the only thing that can still vary across those 13 px is HUE.
    // The post FX hue-restoration blend (§12.1) weights by `smoothstep(1,2,peak)`, which
    // rises and falls across the band, so G swung 185 -> 170 -> 231: a saturated ring
    // sandwiched between two paler ones. That is the "colour rings instead of a gradient"
    // report, and it is a plateau problem, not a brightness problem — halving the
    // strengths only took the clipped area from 10924 px to 6940 and kept the dip.
    // A cubic falls away immediately (slope -3/thickness at gap = 0), so the pinned core
    // is a 5 px sliver with a monotone ramp on either side of it: 13 -> 5 px flat top,
    // 10924 -> 7029 clipped pixels, and G rises monotonically the whole way.
    float t = clamp(gap / u_contactThickness, 0.0, 1.0);
    float m = 1.0 - t;
    return m * m * m;
}

float impactRipple() {
    if (u_impactAge > 4.0) return 0.0;
    float d = distance(fragPosition, u_impactView);
    float wave = sin(d * u_rippleFrequency - u_impactAge * u_rippleSpeed);
    return max(wave, 0.0) * exp(-d) * exp(-u_impactAge);
}

void main() {
    vec3 viewDir = normalize(shieldViewDir);
    vec3 normal = normalize(fragNormal);
    // PER PIXEL. These were varyings computed in the vertex shader, which meant a QUARTIC
    // sampled at the vertices and joined with straight lines across each quad of a
    // 40-slice sphere. Everything the shell shades with hangs off them — wall density,
    // path length, the rim's white threshold, the emission mask — so all of it broke into
    // flat strips with a crease at every mesh ring, which is what read as an unsmooth
    // colour transition at the silhouette and along the ground line. The normal is
    // already interpolated and normalised here; evaluating the curve from it costs a dot
    // and three multiplies and the facets are gone.
    float shieldNdotV = abs(dot(normal, viewDir));
    float fresnelM = 1.0 - shieldNdotV;
    float fresnelX2 = fresnelM * fresnelM;
    float fresnel = fresnelX2 * fresnelX2;
    float t = u_time * u_flowSpeed;
    vec2 baseUV = fragTexCoord * u_noiseScale;
    vec3 flowSample = (u_hasFlow != 0) ? texture(u_flowTex, baseUV).rgb :
                      ((u_hasPacked != 0) ? texture(u_packedTex, baseUV).rgb : vec3(0.5, 0.5, 1.0));
    vec2 flow = (flowSample.rg * 2.0 - 1.0) * u_flowStrength;
    vec2 innerUV = baseUV + flow * (t + 1.0) + shieldViewDir.xy * u_parallaxDepth * u_innerDepth;
    vec3 packed = (u_hasPacked != 0) ? texture(u_packedTex, innerUV).rgb : vec3(0.0, 0.0, 1.0);
    float energy = max(packed.r, packed.g);
    float noise = packed.g;
    float softMask = (u_hasPacked != 0) ? packed.b : 1.0;
    float contact = depthContact(gl_FragCoord.xy / u_resolution);
    float ripple = impactRipple();
    vec3 lightDir = normalize(u_lightDirView);
    float rearInterface = (u_wallPass == 0) ? 1.0 : 0.0;
    // Wall thickness: path length through the shell, 1/|N.V|, turned into density by
    // Beer-Lambert. Hoisted here because it drives BOTH the rim colour and the alpha.
    float pathLen = 1.0 / max(shieldNdotV, 0.10);
    float wallDensity = 1.0 - exp(-u_fresnelAlpha * 3.0 * (pathLen - 1.0));
    float light = max(dot(normal, lightDir), 0.0);
    float pattern = smoothstep(0.22, 0.78, energy) * (0.35 + 0.65 * noise);
    float filament = smoothstep(0.58, 0.82, energy) *
                     smoothstep(0.42, 0.76, noise);
    // NO FAKE GROUND CONTACT. This was `smoothstep(0.05, 0.92, -normal.y)` — a band in
    // NORMAL space, which on a sphere projects to an ellipse on screen: a hard seam
    // across the lower half with no physical meaning, and no relationship to where the
    // shell actually meets the ground. Narrowing it to the bottom cap only sharpened the
    // seam. A soft fake is still a fake, and the honest state is to draw nothing here.
    //
    // The real mechanism is depthContact(), which traces the shell's intersection with
    // whatever it stands on and follows uneven terrain for free. It is wired end to end
    // (uniform set, texture bound, `hasDepth=1` verified by probe) and currently returns
    // 0 for every fragment — see the commit that fixed its radial-vs-axial distance bug
    // and localised the remainder to the depth snapshot. When that lands, contact comes
    // back correctly and this term is not needed.
    float bottomGlow = 0.0;
    // Keep the carrier readable on white backgrounds: a low mass floor plus
    // structured pattern, while the semantic Magic appearance supplies the
    // stronger radiance through the separate emission pass.
    float bodyStructure = smoothstep(0.18, 0.72, pattern);
    /* Preserve hue without laying a milky, high-luminance film over a bright
     * destination.  The carrier is deliberately darker/sparser; rim + the
     * separate additive emission pass provide the perceived brightness. */
    vec3 body = u_bodyColor.rgb * (0.055 + 0.11 * light + bodyStructure * 0.24);
    body = pow(max(body, vec3(0.0)), vec3(1.12));
    body += u_rimColor.rgb * bottomGlow * 0.75;
    vec2 matcapUV = normal.xy * 0.5 + 0.5;
    vec3 matcap = (u_hasMatcap != 0) ? texture(u_matcapTex, matcapUV).rgb
                                     : vec3(0.25 + normal.y * 0.25);
    // THE OUTER EDGE GOES WHITE, the saturated hue sits just inside it. A rim that
    // saturates to its own colour at its brightest reads as a thick painted outline;
    // real energy shells put a near-white hot line at the silhouette with the element
    // hue as the band behind it — §5.4's "hot core may desaturate, the corona carries
    // the hue". The blend keys on wall thickness, so white appears only where the shell
    // is edge-on and thickest.
    // NARROW. §5.4: the white core is the thinnest band and the saturated corona is
    // wider than it — the first attempt whitened from 0.62 density outward, which is
    // most of the visible rim, and cost 0.25 of chroma (0.553 -> 0.306 on the dark
    // background) for an edge that then read as pale rather than hot.
    vec3 rimHot = mix(u_rimColor.rgb, vec3(1.0), smoothstep(0.90, 0.998, wallDensity));
    vec3 glow = rimHot * (fresnel * u_rimStrength) +
                u_rimColor.rgb * (pattern * 0.35 + ripple * 1.5);
    glow += matcap * fresnel * 0.55;
    glow += u_rimColor.rgb * bottomGlow * (0.65 + pattern * 1.15);
    // A HOT CORE LIKE THE RIM'S, BUT THE WINDOW HAS TO MATCH THE BAND'S OWN PEAK.
    // The first attempt keyed the white on smoothstep(0.88, 0.995, contact) — copied from
    // `rimHot`'s narrow window — and it read as a SEPARATE PALE STRIPE sitting inside the
    // orange one with a hard edge between them, not as a hot core. Two things make that
    // window wrong here:
    //
    //   - the band's luminance is already near the ceiling across its width (measured, 8
    //     consecutive pixels with R pinned at 255), so white cannot make anything
    //     brighter; it can only shift hue, and a hue shift with no luminance change is
    //     read as a different band, not a hotter one;
    //   - after the profile went cubic, `contact` spends very little of its range above
    //     0.88, so that shell of values lands OFF the visible band's peak instead of on
    //     it — a white line beside the bright part rather than in it.
    //
    // 0.75 -> 1.0 ramps the white in across the range the band actually occupies, so the
    // whitest pixel is the brightest pixel and the eye reads one hot line with a hue
    // corona, the same structure as the silhouette. Term ablation confirmed this is the
    // term that owns the artefact: removing the matcap or `rimHot` changed nothing.
    vec3 contactHot = mix(u_contactColor.rgb, vec3(1.0), smoothstep(0.75, 1.0, contact));
    // HELD BACK OUT OF `glow` until after the rear attenuation below. The contact line is
    // the one emission term that is a property of the SURFACE THE SHELL TOUCHES rather
    // than of which wall you are looking at, and the far wall is where half of that line
    // lives. Folding it in would dim exactly the half this fix exists to restore.
    vec3 contactGlow = contactHot * contact * u_contactStrength;

    /* The rear optical interface must remain present, but it cannot be an
     * indistinguishable duplicate of the front shell.  Give it a darker,
     * quieter carrier so the eye reads a real inner volume instead of one
     * flat translucent disc. */
    /* declared earlier — see the hoist above bottomGlow */
    /* Rear coverage is now intentionally visible; do not attenuate its body
     * a second time or the 0.45 volume term collapses to a barely measurable
     * tint after the shared bodyOpacity multiplier. */
    body *= mix(1.0, 1.35, rearInterface);
    /* 0.42, DOWN FROM 0.68, because the emission pass now draws this wall too. While it
     * drew the near wall only the far wall contributed no radiance at all, so the weight
     * was academic; with both walls drawing, the broad angle-independent terms (`pattern`,
     * the matcap sheen) land twice on every interior pixel, and the doubled light
     * cancelled the carrier's attenuation — on the cool plate darken% fell 73.5 -> 12.1
     * while the shell was still, correctly, a piece of glass. The far wall is also seen
     * THROUGH the near one, so arriving at ~0.4 is the honest ordering. */
    glow *= mix(1.0, 0.42, rearInterface);
    glow += contactGlow;

    /* Safe scene-through glass: the C side binds a copy made after the 3D
     * scene is complete.  This is what gives the shield a real volume instead
     * of a flat tinted disc; the authored carrier remains on top for colour. */
    if (u_hasScene != 0 && u_emissionOnly == 0) {
        vec2 sceneUV = gl_FragCoord.xy / u_resolution;
        sceneUV += flow * 0.018 * (0.35 + fresnel);
        vec3 behind = texture(u_sceneTex, sceneUV).rgb;
        /* Scene dominates the membrane; the authored tint only colours the
         * glass, otherwise a flat QA background makes this indistinguishable
         * from the legacy opaque carrier. */
        vec3 glassTint = mix(behind * 0.92, body, 0.18 + 0.22 * pattern);
        body = mix(body, glassTint, 0.92 * softMask);
    }

    if (u_emissionOnly != 0) {
        // Emission is sparse radiance, not a second translucent body.  A
        // non-zero floor here paints the entire sphere on bright backgrounds
        // and defeats the shared Magic body/emission separation.
        float emissionMask = max(fresnel * 0.92,
                                 max(filament * 0.0,
                                     max(contact * 0.90, ripple)));
        float emissionAlpha = u_opacity * clamp(emissionMask, 0.0, 1.0);
        finalColor = VFX_ResolveEmission(glow, u_emissionGain, 1.0, emissionAlpha);
        return;
    }
    /* Keep the rear interface for thickness/parallax, but make it a light
     * optical contribution.  Removing it flattens the shell; weighting it too
     * strongly is what creates the milky full-sphere wash on bright backdrops. */
    float wallWeight = (u_wallPass == 0) ? 0.86 : 1.0;
    // WALL THICKNESS, not a fresnel band. The rim term used to be shieldFresnel, which
    // is (1-|N·V|)^4: essentially zero across the middle of the sphere and then a spike
    // at the silhouette, so the shell read as a flat interior with a hard ring stuck to
    // its edge. A shell's optical depth is the PATH LENGTH through its wall, 1/|N·V|,
    // and Beer-Lambert turns that into coverage that rises smoothly all the way from the
    // centre out. At |N·V| = 0.75 this gives 0.064 against the old 0.004 — sixteen times
    // more presence in the mid-region, which is the whole of the "rim to centre" ramp.
    float alpha = u_opacity * wallWeight * softMask *
                  (u_baseAlpha + wallDensity +
                   contact * u_contactAlpha + ripple * 0.18);
    /* Rear glass carries interior volume — but SHAPED BY ITS OWN THICKNESS, not as a
     * constant. A flat term veils the whole interior evenly, which is a milky film with
     * no information in it: the sphere still reads as a rim with fog behind it.
     *
     * What makes a glass sphere read as a sphere is that you see the far wall THROUGH
     * the near one, and the far wall has its own grazing-angle gradient — thin where you
     * look straight through the middle, dense toward the edges. Two nested gradients,
     * not one ring and a haze. So the rear term rides the same Beer-Lambert density the
     * front wall uses, which is what puts a second, inner falloff inside the outer rim.
     * The small constant that remains is the residue that keeps the very centre from
     * vanishing entirely. */
    alpha += rearInterface * u_opacity * softMask * (0.10 + 0.62 * wallDensity);
    /* The 0.35 that used to be here was compensation for a model that no longer
     * exists. It was added to tame a milky sphere back when the rim term was a
     * (1-|N.V|)^4 band with a flat rear-wall veil on top: both of those laid coverage
     * across the middle of the sphere, so the only way to get a scene-through window
     * was to scale everything down. The wall is now Beer-Lambert on path length and the
     * rear rides the same density, which are both ~0 face-on by construction — the
     * window is a property of the model instead of a multiplier fighting it, and the
     * multiplier now only starves the rim it was never aimed at.
     *
     * Interior coverage is `shield_shell_base_alpha` (live in tuning.cfg), which is what
     * to reach for if the glass should read denser. */
    finalColor = VFX_ResolvePremultiplied(
        body + u_rimColor.rgb * fresnel * 0.45, u_bodyOpacity, alpha,
        vec3(0.0), 0.0, 0.0);
}
