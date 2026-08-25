#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// Textured water-energy shield.  Unlike glass_shell.fs, this shader has no
// camera-space window: every transparent gap is read from the moving membrane
// sheet itself, so the silhouette remains organic as the camera moves.
//
// ── 25/08/2026: IT WAS AN OPAQUE PLANET, AND THE REFERENCE IS A BUBBLE ───────
//
// Measured before this pass: cover 11.99%, body 11.74%, darken 93.5% on the
// white plate. Those are the numbers of a solid ball, and that is what it
// looked like — a blue marble with weather on it. Three separate causes, none
// of which is "the colour is wrong":
//
//   1. COVERAGE. `liquidCarrier` reached 1.62 and multiplied a 0.435 base, so a
//      single layer hit ~0.70 — and the shell is drawn TWICE per pass (far wall
//      then near wall), which compounds to ~0.91. Nothing could be seen through
//      it at any angle.
//   2. THE SCENE WAS BEING PAINTED OVER. `mix(refractedScene, liquidTint, ...)`
//      summed to ~1.0 on any lit fold, so even where alpha allowed the
//      background through, the colour did not carry it.
//   3. THE STRUCTURE WAS THE WRONG KIND OF SIGNAL. `cloud_noise.png` is area
//      fbm: statistically homogeneous, every region looking like every other,
//      which is exactly the "necklace" failure vc_shock_ring.inl documents at
//      length. The reference's energy is in THIN BRANCHING FILAMENTS and
//      discrete points — line structures, not a field of blobs. No amount of
//      retinting an fbm produces one. (The points are a particle job and were
//      removed from this shader again on the same day; see below.)
//
// So: the carrier is now genuinely transparent, the refracted scene is the
// dominant term in the interior, and the structure is a ridged-noise vein
// system plus a sparkle lattice. The reference orb's GREEN is its element, not
// its identity — this shader is fed VC_MaterialId by the caller and must stay
// hue-agnostic, so what is copied here is the transparency, the filaments, the
// sparks and the value distribution.
//
// ANCHORING, and why the noise is 3D. `fragPosition`/`fragNormal` arrive in
// VIEW space (glass_shell.vs explains why), so noise keyed on either swims when
// the camera orbits. `fragTexCoord` is the sphere's own UV and is stable, but
// sampling 2D noise on it seams down u = 0 and pinwheels at the poles. The
// direction reconstructed from that UV is both stable and seamless, so the
// veins live in 3D noise sampled along it.
in vec3 shieldViewDir;

uniform vec4 u_bodyColor;
uniform vec4 u_rimColor;
uniform float u_opacity;
uniform float u_bodyOpacity;
uniform float u_emissionGain;
uniform int u_emissionOnly;
// Primary sheet follows the engine's geometry-material contract: the
// immediate-mode mesh binds it through rlSetTexture(), which is texture0.
uniform sampler2D texture0;
uniform sampler2D u_flowTex;
uniform sampler2D u_sceneTex;
uniform int u_hasScene;
uniform sampler2D u_depthTex;
uniform int u_hasDepth;
uniform float u_depthEnabled;
uniform float u_contactThickness;
uniform float u_time;
uniform float u_flowSpeed;
uniform float u_flowStrength;
uniform float u_flowTiling;
uniform float u_refractionStrength;
// The veins get their OWN frequency, deliberately not u_flowTiling. That one
// sizes the membrane sheet, and reusing it here tied thread width to sheet
// scale — at its 1.15 default the "threads" came out roughly a third of the
// sphere across, which is why they read as vapour swirls rather than as a
// filament network. Separate knob, separate concern.
uniform float u_veinScale;
uniform float u_veinSharp;
// ── TWO VARIANT SELECTORS, AND THEY ARE NOT TUNING VALUES ───────────────────
// These pick a CODE PATH, not a magnitude (ENGINE_LANDMINES.md 13: a persisted
// A/B knob that selects a variant silently runs different code, and a stale
// tuning.cfg then makes a measurement describe an effect nobody is looking at).
// They exist so "which noise, which flow" can be answered by comparing renders
// instead of by argument. Defaults 0/0 are the shipping look. When one of these
// is not 0, say so in whatever you report.
uniform int u_veinMode;   // 0 ridged | 1 warped | 2 cellular | 3 stretched
uniform int u_flowMode;   // 0 drift  | 1 swirl  | 2 convection

const float SHIELD_TAU = 6.28318530718;

// The sphere's own object-space direction, rebuilt from its UV. Stable under
// camera rotation (unlike anything in view space) and continuous across the u
// seam and both poles (unlike 2D noise on the raw UV).
vec3 ShieldDir(vec2 uv)
{
    float th = uv.x * SHIELD_TAU;
    float ph = uv.y * 3.14159265;
    float s = sin(ph);
    return vec3(s * cos(th), cos(ph), s * sin(th));
}

// ShieldRidge / ShieldRidgedFbm / the warped variant moved to
// core/shaders/common/noise.glsl as ridged3 / ridgedFbm3 / filaments3 on
// 25/08/2026, once the energy orb and the volume trail wanted the same field.
// Only the mode selector and the cellular candidate remain local.

// Cellular F2-F1: the boundary between neighbouring feature points, which is a
// CRACK network rather than a ridge network — hard straight-ish joints meeting
// at nodes, where ridged noise gives soft meandering threads. Costs 27 hash3
// calls, so it is the expensive candidate; that matters only if it wins.
float ShieldCells(vec3 p)
{
    vec3 ip = floor(p);
    vec3 fp = p - ip;
    float f1 = 9.0;
    float f2 = 9.0;
    for (int k = -1; k <= 1; k++)
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        vec3 g = vec3(float(i), float(j), float(k));
        vec3 o = vec3(hash3(ip + g), hash3(ip + g + 31.7), hash3(ip + g + 71.3));
        float d = length(g + o - fp);
        if (d < f1) { f2 = f1; f1 = d; }
        else if (d < f2) { f2 = d; }
    }
    // 1 at a cell boundary, falling off into the cell interior.
    return clamp(1.0 - (f2 - f1) * 1.35, 0.0, 1.0);
}

float ShieldVeins(vec3 p)
{
    if (u_veinMode == 1)
        return filaments3(p, 2.6);
    if (u_veinMode == 2)
        return clamp(ShieldCells(p * 0.72) * 0.82 + ridgedFbm3(p) * 0.28, 0.0, 1.0);
    if (u_veinMode == 3) {
        // STRETCHED: compress one axis of the sample domain so features
        // elongate across it. Filaments become currents running one way rather
        // than an isotropic web.
        vec3 q = vec3(p.x, p.y * 0.28, p.z);
        return ridgedFbm3(q);
    }
    return ridgedFbm3(p);
}

// Where the sample point travels over the shell's life.
vec3 ShieldFlowOffset(vec3 dir, float drift)
{
    if (u_flowMode == 1) {
        // SWIRL about the shell's own axis: rotate the sample domain, so the
        // whole network turns as one body instead of sliding through itself.
        float a = drift * 0.42;
        float c = cos(a), sn = sin(a);
        return vec3(dir.x * c - dir.z * sn, dir.y, dir.x * sn + dir.z * c) - dir;
    }
    if (u_flowMode == 2)
        // CONVECTION: pushed from the poles toward the equator, so energy
        // reads as arriving at the widest part of the shell.
        return vec3(0.0, -drift * 0.9 * sign(dir.y) * abs(dir.y), 0.0);
    return vec3(0.0, -drift * 0.55, drift * 0.22);
}

// SPARKS DELETED 25/08/2026, by owner decision and worth recording as one: a
// hashed twinkle lattice was painted onto the shell here, and specks on an
// energy shield are a PARTICLE job. Painted onto the surface they cannot pass
// in front of or behind the shell, cannot outlive it, and cannot be lit — they
// are a texture pretending to be a volume. Do not add them back to this shader.

vec2 FlowedUV(vec2 uv, vec2 direction, float phase)
{
    // Centre the phase around zero: the two samples travel equal distances in
    // opposite parts of the loop, then crossfade at the point both are quiet.
    return fract(uv + direction * ((phase * 2.0) - 1.0) * u_flowStrength);
}

float DepthContact(vec2 uv)
{
    if (u_hasDepth == 0 || u_depthEnabled < 0.5) return 0.0;
    float sceneDepth = texture(u_depthTex, uv).r;
    float fragmentDepth = max(-fragPosition.z, 0.0001);
    float gap = sceneDepth - fragmentDepth;
    // NOTHING BEHIND IS NOT CONTACT. Where no geometry was drawn the depth
    // texture reads 0, so `gap` goes negative, `t` clamps to 0 and the falloff
    // below returns 1 — maximum contact across every pixel of sky. It renders as
    // the shell's whole upper half blown to white with a razor-straight edge at
    // the horizon, which is where the scene stops writing depth. Invisible until
    // the shell was lifted to rest on the ground, because before that it never
    // reached above the horizon. glass_shell.fs already carries this guard;
    // this shader was written without it.
    if (gap <= 0.0) return 0.0;
    float t = clamp(gap / max(u_contactThickness, 0.0001), 0.0, 1.0);
    float falloff = 1.0 - t;
    // Quadratic keeps a broad liquid transition around the terrain contact;
    // the narrow emissive core is selected separately below.
    return falloff * falloff;
}

void main()
{
    vec3 viewDir = normalize(shieldViewDir);
    vec3 normal = normalize(fragNormal);
    float ndotv = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 4.0);

    vec2 baseUV = fragTexCoord * u_flowTiling;
    vec2 flowVector = texture(u_flowTex, fract(baseUV * 0.53 + vec2(0.11, -0.07))).rg * 2.0 - 1.0;
    float phaseA = fract(u_time * u_flowSpeed);
    float phaseB = fract(phaseA + 0.5);
    float flowLerp = abs(phaseA * 2.0 - 1.0);
    vec4 membraneA = texture(texture0, FlowedUV(baseUV, flowVector, phaseA));
    vec4 membraneB = texture(texture0, FlowedUV(baseUV, flowVector, phaseB));
    vec4 membrane = mix(membraneA, membraneB, flowLerp);
    vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
    vec2 refractionUV = clamp(screenUV +
        (flowVector + normal.xy * 0.55) * u_refractionStrength *
        (0.35 + 0.65 * fresnel), vec2(0.001), vec2(0.999));
    vec3 refractedScene = (u_hasScene != 0) ? texture(u_sceneTex, refractionUV).rgb
                                             : u_bodyColor.rgb;

    // ── STRUCTURE ───────────────────────────────────────────────────────────
    // The sheet is kept, but demoted: it is now a slow, low-contrast BODY
    // gradient — where the water is thicker — and no longer pretends to be the
    // shield's detail. Its RGB is never emitted, so dark source texels cannot
    // punch black seams in a transparent carrier.
    float filamentField = dot(membrane.rgb, vec3(0.2126, 0.7152, 0.0722));
    float liquidDensity = clamp(filamentField, 0.0, 1.0);
    float flowDetail = smoothstep(0.18, 0.78, liquidDensity);

    // The veins. Advected by drifting the 3D sample point, not by scrolling a
    // UV, so they slide over the surface without the sphere's parameterisation
    // showing through. Raised to a high power because a ridge crest is broad
    // and the reference's threads are one or two pixels of very bright line.
    vec3 dir = ShieldDir(fragTexCoord);
    float drift = u_time * u_flowSpeed;
    vec3 vp = dir * max(u_veinScale, 0.5)
            + ShieldFlowOffset(dir, drift) * max(u_veinScale, 0.5)
            + vec3(flowVector * (u_flowStrength * 1.4), 0.0);
    // EVEN DISTRIBUTION IS DELIBERATE. A large-scale field was gating this
    // network into dense and clear regions, on the general argument that an fbm
    // is statistically homogeneous and an evenly-spread threshold reads as
    // procedural (vc_shock_ring.inl documents that failure at length). The
    // owner judged the clumped version against the reference and chose even:
    // a containment field is a regular structure, and its regularity is the
    // point. Left as a note so the next reader does not "fix" it back.
    float veinField = ShieldVeins(vp);
    // A ridge crest is broad by nature. The threshold takes only its top, and
    // the power thins what is left to a line; both are exposed because "how
    // thin is a thread" is a look decision, not a derivation.
    float veins = pow(smoothstep(0.62, 1.0, veinField), max(u_veinSharp, 1.0));
    float veinHalo = pow(smoothstep(0.34, 0.98, veinField), 3.0) * 0.13;


    float interfaceWeight = gl_FrontFacing ? 1.0 : 0.62;
    // The WIDE edge gradient: the water volume thickens toward grazing angles
    // well before the thin emissive silhouette takes over.
    float edgeGradient = smoothstep(0.10, 0.82, sqrt(fresnel));
    float rimCore = smoothstep(0.38, 0.84, sqrt(fresnel));

    // ── COVERAGE: this is where the planet became a bubble ──────────────────
    // Every term here is per-LAYER and the shell draws two of them (far wall,
    // then near wall), so a value of a composites to 1-(1-a)^2. The interior
    // base is therefore deliberately small: 0.052 twice is ~0.10 total, which
    // is a tinted pane of glass rather than a wall. The budget is spent on the
    // rim and on the veins instead, which is how the reference distributes it —
    // mostly see-through, with a few very present features.
    float bodyCoverage = u_opacity * interfaceWeight *
                         (0.034 + 0.110 * edgeGradient + 0.018 * flowDetail);
    // Threads are the only part of a bubble that is genuinely opaque, so they
    // carry most of what the shell can bite out of bright scenery.
    float veinCoverage = u_opacity * interfaceWeight * veins * 0.78;
    float rimCoverage = u_opacity * interfaceWeight *
                        smoothstep(0.42, 0.88, fresnel) * 0.30;
    float contact = DepthContact(screenUV);
    float contactCore = smoothstep(0.72, 0.98, contact);
    float coverage = clamp(bodyCoverage + veinCoverage + rimCoverage +
                           contact * 0.30, 0.0, 1.0);

    // ── COLOUR: the scene behind is the dominant term in the interior ───────
    // THE VEINS ARE PIGMENT IN THIS PASS AND LIGHT IN THE OTHER, and the split
    // is what makes them survive a bright background. An earlier version tinted
    // them toward white here too, which looks right on black and is invisible
    // on a white plate: white ink on white scenery attenuates nothing, so the
    // whole shell measured darken 1.3% and read as a faint smudge. Saturated
    // element hue in the body, white-hot only in the emission pass below.
    vec3 deepWater  = u_bodyColor.rgb * 0.30;
    vec3 veinInk    = mix(u_rimColor.rgb, u_bodyColor.rgb, 0.30) * 0.85;
    vec3 liquidTint = mix(deepWater, u_rimColor.rgb * 0.72,
                          smoothstep(0.15, 0.72, flowDetail));
    liquidTint = mix(liquidTint, veinInk, clamp(veins * 0.95 + contact * 0.40, 0.0, 1.0));
    // Held well below 1: past about 0.55 the refracted scene stops being
    // legible and the shell reads as painted glass again.
    float tintMix = clamp(0.13 + 0.20 * edgeGradient + 0.62 * veins
                          + 0.16 * contact, 0.0, 0.88);
    vec3 body = mix(refractedScene, liquidTint, tintMix);

    // ── EMISSION: concentrated, not spread ──────────────────────────────────
    // A bubble is not uniformly self-lit. Energy collects in the veins, in the
    // along the silhouette and where the shell meets the ground; the
    // broad interior contributes almost nothing, which is what leaves room for
    // the few bright things to read as bright.
    float lowerVolume = smoothstep(0.54, 0.94, fragTexCoord.y);
    float emissionMask = veins * (3.10 + 1.10 * lowerVolume)
                       + veinHalo * 0.18
                       + 0.34 * rimCore
                       + lowerVolume * 0.03
                       + contactCore * (0.90 + 0.35 * veins);

    if (u_emissionOnly != 0) {
        // This draw is still inside MAGIC's premultiplied scope.  Emission
        // therefore needs the same resolver as the body; ResolveEmission is
        // additive-only and would expose straight-alpha dark RGB at holes.
        // NOT `coverage * mask`: coverage is now small almost everywhere, so
        // gating the glow on it would scale the veins down by the very
        // transparency that makes the shell a bubble. The emissive features
        // carry their own presence.
        float glowCoverage = clamp(emissionMask * (0.55 + 0.45 * coverage) *
                                   u_opacity * interfaceWeight, 0.0, 1.0);
        vec3 contactHot = mix(u_rimColor.rgb, vec3(0.72, 0.92, 1.0), contact * 0.45);
        finalColor = VFX_ResolvePremultiplied(contactHot, u_emissionGain,
                                               glowCoverage, vec3(0.0), 0.0, 0.0);
    }
    else
        finalColor = VFX_ResolvePremultiplied(body, u_bodyOpacity, coverage,
                                               u_rimColor.rgb, 0.0, 0.0);
}
