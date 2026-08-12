#version 330
#include "core/shaders/common/noise.glsl"

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // smoothed nearest fluid depth
uniform sampler2D u_thicknessTex; // dual-depth thickness, metres
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
/* The RAW front capture (RGBA32F): .r depth, .g coverage, .b liquid-table
 * slot. Read unfiltered and unsmoothed on purpose — the depth filter averages
 * its taps, and an averaged material id is a different material. */
uniform sampler2D u_materialIdTex;
uniform vec2 u_texel;      // fluid reconstruction texel
uniform vec2 u_sceneTexel; // full-resolution scene texel
uniform int u_hasSceneDepth;
uniform int u_qualityTier;
// World-space radius of one reconstruction kernel (FluidSurface_SetReconstructionRadius).
uniform float u_kernelRadius;

uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_sunDirectionView;  // surface -> sun, view space
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform float u_time;

/* --- The liquid table ----------------------------------------------------
 *
 * One entry per slot; `FluidLiquidDesc` in core/fluid/fluid_surface.h is the
 * authoring side of exactly these lanes. The rgb lanes carry the optical
 * identity the element materials already provide; the alpha lanes carry what a
 * colour cannot say — which BRANCH of the optics this liquid takes, and how hot
 * / how reflective / how crusted it is.
 *
 * vec4 throughout on purpose. A vec3 uniform ARRAY is the one shape whose std140
 * stride (16 bytes, not 12) disagrees with what SetShaderValueV packs, and the
 * point-light arrays above are already vec4 for the same reason. */
#define FLUID_MATERIAL_SLOTS 4
#define FLUID_CLASS_DIELECTRIC 0
#define FLUID_CLASS_EMISSIVE   1
#define FLUID_CLASS_CONDUCTOR  2
uniform vec4 u_materialBody[FLUID_MATERIAL_SLOTS];   // rgb body, a = class
uniform vec4 u_materialGlow[FLUID_MATERIAL_SLOTS];   // rgb glow, a = emission
uniform vec4 u_materialSoft[FLUID_MATERIAL_SLOTS];   // rgb soft, a = crust/foam gain
uniform vec4 u_materialOptics[FLUID_MATERIAL_SLOTS]; // x ior, y roughness, z opacity/m, w reserved

#define FLUID_POINT_LIGHTS 4
uniform int u_pointLightCount;
uniform vec4 u_pointLightPosRadius[FLUID_POINT_LIGHTS];
uniform vec4 u_pointLightColor[FLUID_POINT_LIGHTS];

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

/* Normal-incidence reflectance of a dielectric interface against air. Water's
 * 0.02037 used to be a literal in three places (here, inside WaterSpecularBRDF,
 * and as the 1.0/1.333 handed to refract) — which is why a liquid that is not
 * water could not be expressed at all. */
float IorToF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

vec3 FresnelSchlick(float ndv, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - ndv, 5.0);
}

// Multi-octave procedural wave generator for realistic dynamic liquid shimmer.
// Ripples roll in all three axes (a planar XZ sheet smears on a sphere
// silhouette), and a patchy high-frequency capillary term adds droplet
// detail that swell alone cannot reproduce.
vec3 WaterMultiOctaveWaves(vec3 worldPosition) {
    float t = u_time * 1.2;
    vec3 p = worldPosition;
    float s1 = sin(p.x * 6.0 + t * 0.8 + sin(p.y * 4.0 + t * 0.6));
    float s2 = sin(p.y * 7.0 - t * 0.7 + sin(p.z * 5.0 + t * 0.9));
    float s3 = sin(p.z * 5.5 + t * 1.1 + sin(p.x * 6.5 - t * 0.5));
    vec2 swell = vec2(s1 + s2, s2 + s3);
    float capFade = 0.5 + 0.5 * sin(dot(p, vec3(3.7, 5.1, 4.3)) + t * 0.7);
    vec2 capillary = vec2(
        sin(p.x * 47.0 + p.y * 29.0 + t * 2.4) + cos(p.z * 41.0 + t * 1.8),
        sin(p.z * 53.0 + p.y * 33.0 + t * 2.1) + cos(p.x * 37.0 + t * 1.6));
    /* The capillary term is the finest detail on the surface and the one that
     * aliases first: its wavelength is smaller than a splat, so at any real
     * viewing distance several periods land inside one pixel. Kept, because it
     * is what stops the body reading as polished plastic — but at a third of
     * its former amplitude, which is below the point where the specular lobes
     * above turn it into rings. */
    vec2 dh = swell * 0.016 + capillary * 0.004 * capFade;
    /* The SLOPE, with no Y component. This used to return a tangent-space NORMAL
     * (the same two slopes, but with a constant 1.0 in Y), which the caller then
     * added straight to a WORLD normal. That is only meaningful through a TBN basis, and there is none
     * here, so what actually happened was a constant +1.0 world-up bias added to
     * every surface normal, modulated by the sine field. On a curved body that
     * paints the iso-lines of the swell onto the surface as ridges: the parallel
     * bands. The pre-multi-octave version (2ce1a2e) got this right by projecting
     * its ripple onto the tangent plane; the rewrite dropped the projection and
     * kept the 1.0. */
    return vec3(-dh.x, 0.0, -dh.y);
}

/* The depth at which this liquid reaches its authored colour, and the one scale
 * the optics below are expressed in.
 *
 * It is MEASURED, not chosen: with dual-depth thickness the debug-2 ruler puts
 * both authored fixtures at about this path through their middle — the water
 * ring's tube is 0.216 m across by construction (ring 0.9 m x
 * WATER_RING_TUBE_RATIO 0.12, doubled), and the PBD crown reads 0.16-0.25 m.
 * So "as deep as an authored body is through the middle" is 0.20 m, and
 * Beer-Lambert at that depth should deliver exactly the material colour.
 *
 * This replaces a chain of hand-tuned constants that each had to compensate for
 * the one before it: an invented kernel-overlap divisor, an invented saturation
 * knee, and an absorption scale re-tuned by eye twice against both. */
#define FLUID_REFERENCE_DEPTH_M 0.20
/* Extinction that is not the body colour — the liquid's own turbidity. Set so
 * that one reference depth of it costs 0.06 of optical depth, which is what the
 * previous absorption term added at the same place. */
#define FLUID_TURBIDITY_PER_M 0.30

/* Thickness arrives from fluid_thickness_resolve.fs already in metres, measured
 * as z_back - z_front over the same splat cloud the depth pass captured. There
 * is nothing left to decode — which is the point. What stood here before was an
 * additive sum of per-splat sphere chords divided by an invented overlap factor
 * and pushed through an invented saturating knee, and every optical constant
 * downstream had to be re-tuned whenever either moved. */
float DecodeOpticalThickness(float measuredThicknessMetres) {
    return max(measuredThicknessMetres, 0.0);
}

/* Specular antialiasing by normal filtering — Kaplanyan et al. 2016, in
 * Filament's formulation (`normalFiltering`, materialParams
 * specularAntiAliasingVariance/Threshold, defaults 0.15/0.25).
 *
 * The symptom it exists for is exactly the one here: a narrow lobe over a normal
 * field that changes fast in screen space resolves detail the surface does not
 * actually have, and paints it as a hard bright needle. Widening the lobe by
 * hand was tried before this (roughness 0.035 -> 0.090, exponents 190/256 ->
 * 48/96, to kill onion rings) and it is the wrong control: a constant widening
 * is too much where the surface is genuinely smooth and never enough where it is
 * not. The variance of the normal IS the amount of detail being hidden, so it is
 * what the lobe should widen by.
 *
 * Here the normals are reconstructed from depth taps, so they carry splat-scale
 * wobble the water does not have — the term below is largest exactly there, and
 * at the silhouette, where the normal is least trustworthy.
 *
 * `roughness` in this file is PERCEPTUAL (WaterSpecularBRDF squares it to get
 * GGX alpha), which matches Filament's convention, so the round trip is
 * alpha = r^2, filter on alpha^2, and back. */
#define FLUID_SPECULAR_AA_VARIANCE 0.15
#define FLUID_SPECULAR_AA_THRESHOLD 0.25
float NormalFilteredRoughness(vec3 N, float perceptualRoughness) {
    vec3 dNdx = dFdx(N);
    vec3 dNdy = dFdy(N);
    float variance = FLUID_SPECULAR_AA_VARIANCE * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    float kernelRoughness = min(2.0 * variance, FLUID_SPECULAR_AA_THRESHOLD);
    float alpha = perceptualRoughness * perceptualRoughness;
    float squareAlpha = clamp(alpha * alpha + kernelRoughness, 0.0, 1.0);
    return sqrt(sqrt(squareAlpha));
}

/* How much the two hand-rolled Blinn lobes must widen to match. A Blinn exponent
 * behaves like 2/alpha^2, so scaling it by the ratio of the unfiltered to the
 * filtered alpha^2 keeps all three lobes describing one surface. Without this
 * the GGX term would soften while the sharp glint kept drawing the same needle. */
float BlinnExponentScale(float perceptualRoughness, float filteredRoughness) {
    float alpha = perceptualRoughness * perceptualRoughness;
    float filteredAlpha = filteredRoughness * filteredRoughness;
    return clamp((alpha * alpha) / max(filteredAlpha * filteredAlpha, 1e-8), 0.02, 1.0);
}

/* F0 is a COLOUR, not a scalar: a conductor's reflectance is wavelength
 * dependent and that tint is most of what makes metal read as metal. */
vec3 WaterSpecularBRDF(vec3 N, vec3 V, vec3 L, float roughness, vec3 f0) {
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float ndh = max(dot(N, H), 0.0);
    float vdh = max(dot(V, H), 0.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / max(3.14159265 * denominator * denominator, 1e-5);
    float k = (roughness + 1.0);
    k = k * k * 0.125;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    vec3 F = FresnelSchlick(vdh, f0);
    /* The clamp sits on the FULL product, F included, exactly where the scalar
     * version had it. Clamping D*G first and multiplying F afterwards is a
     * different (much tighter) limiter: at water's F0 the peak lobe would come
     * out 50x dimmer. */
    return min(D * gv * gl * F / max(4.0 * ndl * ndv, 1e-4), vec3(8.0));
}

bool SceneSampleMatchesBase(vec2 uv, float baseSceneDistance, float fluidDistance) {
    if (u_hasSceneDepth == 0) return true;
    float sceneDepth = texture(u_sceneDepthTex, uv).r;
    bool baseHasGeometry = baseSceneDistance < 1e19;
    bool sampleHasGeometry = sceneDepth < 0.99999;
    if (baseHasGeometry != sampleHasGeometry) return false;
    if (!sampleHasGeometry) return true;
    vec3 scenePosition = ReconstructViewPosition(uv, sceneDepth);
    float sampleDistance = -scenePosition.z;
    if (sampleDistance <= fluidDistance + 0.002) return false;
    float layerTolerance = max(0.060, baseSceneDistance * 0.018);
    return abs(sampleDistance - baseSceneDistance) <= layerTolerance;
}

// Lightweight Screen Space Reflection (SSR) raymarcher
vec4 TraceSSR(vec3 rayOrigin, vec3 rayDir, float fluidDistance) {
    if (u_hasSceneDepth == 0 || rayDir.z >= 0.05) return vec4(0.0);
    
    float stepSize = 0.035;
    vec3 currentRay = rayOrigin + rayDir * stepSize;
    
    for (int i = 0; i < 14; i++) {
        vec4 clip = u_projection * vec4(currentRay, 1.0);
        if (clip.w <= 0.0) break;
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sceneDepth = texture(u_sceneDepthTex, sampleUV).r;
        if (sceneDepth < 0.99999) {
            vec3 scenePos = ReconstructViewPosition(sampleUV, sceneDepth);
            float sceneDist = -scenePos.z;
            float rayDist = -currentRay.z;
            
            if (rayDist >= sceneDist && rayDist - sceneDist < 0.25) {
                // Binary refinement step
                currentRay -= rayDir * (stepSize * 0.5);
                clip = u_projection * vec4(currentRay, 1.0);
                sampleUV = clamp(clip.xy / clip.w * 0.5 + 0.5, 0.001, 0.999);
                
                vec3 hitColor = texture(u_sceneTex, sampleUV).rgb;
                float edgeFade = smoothstep(0.0, 0.08, sampleUV.x) * smoothstep(1.0, 0.92, sampleUV.x) *
                                 smoothstep(0.0, 0.08, sampleUV.y) * smoothstep(1.0, 0.92, sampleUV.y);
                return vec4(hitColor, edgeFade * 0.85);
            }
        }
        currentRay += rayDir * stepSize;
        stepSize *= 1.25;
    }
    return vec4(0.0);
}

void main() {
    float fluidDepth = texture(texture0, fragTexCoord).r;

    /* The silhouette had no antialiasing at all: a binary discard on the depth
     * mask cut it into a one-pixel staircase, which at any real zoom is the most
     * visible thing left on the body.
     *
     * The soft ramp that should have feathered it already existed and was on the
     * wrong side of this test. `surfaceCoverage` below fades with thickness, and
     * thickness is Gaussian-blurred, so its ramp lands OUTSIDE the depth mask —
     * inside the mask it has already reached full value, and every pixel the mask
     * kept was therefore fully opaque right up to the cut.
     *
     * So dilate the mask by one texel into that ramp instead of adding a new
     * coverage estimate. A fringe pixel with no depth of its own borrows the
     * nearest cardinal neighbour's, and its alpha comes from the blurred
     * thickness, which is small there by construction. Cost: the thickness tap
     * moves up from where it was already being read, and the four neighbour taps
     * only happen on the fringe, never on the 88% of the screen that is empty. */
    float thicknessProxy = max(texture(u_thicknessTex, fragTexCoord).r, 0.0);
    bool dilatedFringe = false;
    if (fluidDepth >= 0.99999) {
        if (thicknessProxy <= 0.0002) discard;
        dilatedFringe = true;
        float dilateL = texture(texture0, fragTexCoord - vec2(u_texel.x, 0.0)).r;
        float dilateR = texture(texture0, fragTexCoord + vec2(u_texel.x, 0.0)).r;
        float dilateD = texture(texture0, fragTexCoord - vec2(0.0, u_texel.y)).r;
        float dilateU = texture(texture0, fragTexCoord + vec2(0.0, u_texel.y)).r;
        fluidDepth = min(min(dilateL, dilateR), min(dilateD, dilateU));
        if (fluidDepth >= 0.99999) discard;
    }

    /* Which liquid is this pixel?
     *
     * The id rides the same fragment that won the capture's depth test, so it is
     * the material of the surface actually being shaded. It is read from the RAW
     * capture rather than from the smoothed depth `texture0`: the narrow-range
     * filter is an average over neighbours, and averaging slot 0 with slot 2
     * yields slot 1 — a third liquid that is not there.
     *
     * A dilated fringe pixel has no capture of its own (that is what dilation
     * means), so it borrows the nearest cardinal neighbour's slot, matching where
     * its depth came from. Rounding, not truncation: the capture is POINT
     * filtered, but the composite target and the capture are the same size only
     * at HIGH — at MED/LOW the tap can land between texels. */
    float rawSlot = texture(u_materialIdTex, fragTexCoord).b;
    if (dilatedFringe) {
        float sl = texture(u_materialIdTex, fragTexCoord - vec2(u_texel.x, 0.0)).b;
        float sr = texture(u_materialIdTex, fragTexCoord + vec2(u_texel.x, 0.0)).b;
        float sd = texture(u_materialIdTex, fragTexCoord - vec2(0.0, u_texel.y)).b;
        float su = texture(u_materialIdTex, fragTexCoord + vec2(0.0, u_texel.y)).b;
        rawSlot = max(max(sl, sr), max(sd, su));
    }
    int slot = clamp(int(rawSlot + 0.5), 0, FLUID_MATERIAL_SLOTS - 1);
    vec3 materialBody = u_materialBody[slot].rgb;
    vec3 materialGlow = u_materialGlow[slot].rgb;
    vec3 materialSoft = u_materialSoft[slot].rgb;
    int liquidClass = int(u_materialBody[slot].a + 0.5);
    float emissionStrength = u_materialGlow[slot].a;
    float crustGain = u_materialSoft[slot].a;
    float materialIor = max(u_materialOptics[slot].x, 1.0001);
    float roughnessScale = max(u_materialOptics[slot].y, 0.01);
    float extraOpacityPerM = max(u_materialOptics[slot].z, 0.0);

    vec3 positionView = ReconstructViewPosition(fragTexCoord, fluidDepth);
    vec3 V = normalize(-positionView);
    float fluidDistance = -positionView.z;

    float sceneDepthAtSurface = 1.0;
    float sceneDistanceAtSurface = 1e20;
    float intersectionVisibility = 1.0;
    if (u_hasSceneDepth != 0) {
        sceneDepthAtSurface = texture(u_sceneDepthTex, fragTexCoord).r;
        if (sceneDepthAtSurface < 0.99999) {
            vec3 sceneAtSurface = ReconstructViewPosition(fragTexCoord, sceneDepthAtSurface);
            sceneDistanceAtSurface = -sceneAtSurface.z;
            float frontGap = sceneDistanceAtSurface - fluidDistance;
            if (frontGap <= -0.002) discard;
            intersectionVisibility = smoothstep(-0.002, 0.012, frontGap);
        }
    }

    // Minimum Gradient Normal Reconstruction: compare forward/backward pairs
    // to prevent edge bleeding and sharp 90-degree normal spikes at splat edges.
    float depthLeft = texture(texture0, fragTexCoord - vec2(u_texel.x, 0.0)).r;
    float depthRight = texture(texture0, fragTexCoord + vec2(u_texel.x, 0.0)).r;
    float depthDown = texture(texture0, fragTexCoord - vec2(0.0, u_texel.y)).r;
    float depthUp = texture(texture0, fragTexCoord + vec2(0.0, u_texel.y)).r;
    
    /* A MISSING neighbour must be excluded from the choice, not substituted.
     * Substituting the centre's own depth (what this did) puts the fabricated
     * sample at the same distance as the centre, so its z-difference is ~0 — and
     * the minimum-gradient rule below then picks it EVERY time, over the real
     * one. Every silhouette pixel with one missing neighbour therefore had its
     * gradient forced flat along that axis, i.e. its normal forced to face the
     * camera; which neighbour is missing changes pixel to pixel along the edge,
     * so the normal flips between real and view-facing and draws stripes. Left/
     * right missing gives vertical stripes, up/down horizontal — both of which
     * the sandbox showed, on the ring's rim and all over the orb, in the NORMAL
     * view while the unfiltered capture was clean. */
    bool hasL = depthLeft < 0.99999;
    bool hasR = depthRight < 0.99999;
    bool hasD = depthDown < 0.99999;
    bool hasU = depthUp < 0.99999;

    vec3 posL = ReconstructViewPosition(fragTexCoord - vec2(u_texel.x, 0.0), depthLeft);
    vec3 posR = ReconstructViewPosition(fragTexCoord + vec2(u_texel.x, 0.0), depthRight);
    vec3 posD = ReconstructViewPosition(fragTexCoord - vec2(0.0, u_texel.y), depthDown);
    vec3 posU = ReconstructViewPosition(fragTexCoord + vec2(0.0, u_texel.y), depthUp);

    vec3 dx1 = positionView - posL;
    vec3 dx2 = posR - positionView;
    vec3 dy1 = positionView - posD;
    vec3 dy2 = posU - positionView;

    /* With both sides present, keep the minimum-gradient pick — that is what
     * stops a splat's far edge bleeding into the normal. With one side missing,
     * take the side that exists. With neither, fall back to a tangent that is
     * flat along this axis, which is the honest answer for an isolated pixel. */
    vec3 dx = (hasL && hasR) ? (abs(dx1.z) < abs(dx2.z) ? dx1 : dx2)
            : (hasL ? dx1 : (hasR ? dx2 : vec3(positionView.x * u_texel.x, 0.0, 0.0)));
    vec3 dy = (hasD && hasU) ? (abs(dy1.z) < abs(dy2.z) ? dy1 : dy2)
            : (hasD ? dy1 : (hasU ? dy2 : vec3(0.0, positionView.y * u_texel.y, 0.0)));

    vec3 N = normalize(cross(dx, dy));
    if (dot(N, V) < 0.0) N = -N;

    // Apply multi-octave dynamic wave perturbation
    vec3 worldPosition = (u_viewToWorld * vec4(positionView, 1.0)).xyz;
    vec3 worldNormal = normalize(mat3(u_viewToWorld) * N);
    /* Project the wave slope onto the surface's tangent plane before adding it.
     * Without this the perturbation carries a component ALONG the normal, which
     * does not tilt the surface at all — it just rescales the vector before the
     * normalize, so the visible effect is whatever is left over, banded. A
     * perturbation is only a perturbation in the tangent plane. */
    vec3 waveSlope = WaterMultiOctaveWaves(worldPosition);
    waveSlope -= worldNormal * dot(waveSlope, worldNormal);
    worldNormal = normalize(worldNormal + waveSlope * 0.045);
    N = normalize(transpose(mat3(u_viewToWorld)) * worldNormal);
    if (dot(N, V) < 0.0) N = -N;
    float ndv = clamp(dot(N, V), 0.0, 1.0);

    float kernelThickness = DecodeOpticalThickness(thicknessProxy);
    /* The silhouette must FADE, not end. Full opacity at 1 cm of water made a
     * one-splat-deep rim as solid as the body's middle, so every kernel at the
     * boundary drew its own hard dome against the background — the edge that
     * does not match the smooth interior. Fading across a thicker band lets
     * those lumps go translucent and stop competing with the body's shape. */
    /* Sub-pixel coverage of the SILHOUETTE, from the same four neighbour samples
     * the normal reconstruction above already fetched.
     *
     * The thickness ramp cannot do this job. Thickness is Gaussian-blurred, so it
     * only falls below full value several texels OUTSIDE the body — dilating the
     * mask by one texel and taking alpha from thickness just moved the hard edge
     * out by a pixel, which is what the first attempt at this did.
     *
     * A pixel whose centre is inside the surface is at least half covered, and
     * its four neighbours estimate the rest; a dilated fringe pixel has its
     * centre outside, so it gets only the neighbour fraction. Interior pixels
     * come out at exactly 1 and pay nothing, since every tap is already loaded. */
    float insideCount = (hasL ? 1.0 : 0.0) + (hasR ? 1.0 : 0.0)
                      + (hasD ? 1.0 : 0.0) + (hasU ? 1.0 : 0.0);
    float maskCoverage = dilatedFringe ? (0.25 * insideCount)
                                       : (0.5 + 0.125 * insideCount);

    /* Full opacity is reached at one kernel DIAMETER of water — the thinnest
     * thing this reconstruction can represent is a single splat, so anything
     * below that is a rim and should be translucent. Tying the ramp to the
     * kernel keeps it correct when the caller changes the reconstruction
     * radius; the previous literal band was calibrated against the saturating
     * decode and turns into a two-pixel cliff once thickness is metres. */
    float surfaceCoverage = smoothstep(0.0, 2.0 * u_kernelRadius, kernelThickness)
                          * intersectionVisibility * maskCoverage;
    
    float sceneGap = 1.0;
    float waterColumnDepth = kernelThickness;
    if (sceneDepthAtSurface < 0.99999) {
        float depthGap = max(sceneDistanceAtSurface - fluidDistance, 0.0);
        sceneGap = depthGap;
        /* The receiver BOUNDS the water column; it never creates one. The old
         * max() against the gap pinned any airborne body at a constant 0.40 m
         * across its whole silhouette — 2.5x the measured path, with the
         * thickness variation deleted. Absorption follows the thickness pass
         * alone (van der Laan et al. 2009 / Green 2010); the depth gap only
         * shortens it where liquid rests on a receiver, with a 2.2 cm floor so
         * a thin resting sheet does not become invisible. */
        waterColumnDepth = min(kernelThickness, max(0.022, depthGap * 1.25));
    }
    
    /* Grazing lengthens the path; the clamp bounds it at 2.5 reference depths so
     * an edge-on pixel cannot run away to an arbitrary optical depth. */
    float opticalPath = min(waterColumnDepth / max(ndv, 0.22),
                            FLUID_REFERENCE_DEPTH_M * 2.5);
    float airborneWeight = smoothstep(0.070, 0.220, sceneGap);

    // Snell Refraction
    vec3 incident = -V;
    vec3 refractedDirection = refract(incident, N, 1.0 / materialIor);
    vec2 incidentSlope = incident.xy / max(abs(incident.z), 0.25);
    vec2 refractedSlope = refractedDirection.xy / max(abs(refractedDirection.z), 0.25);
    float travelPixels = clamp(opticalPath * 100.0, 0.35, 13.0);
    vec2 refractOffset = (refractedSlope - incidentSlope) * travelPixels * u_sceneTexel;
    vec2 refractUV = clamp(fragTexCoord + refractOffset, u_sceneTexel, vec2(1.0) - u_sceneTexel);

    if (!SceneSampleMatchesBase(refractUV, sceneDistanceAtSurface, fluidDistance)) {
        refractUV = mix(refractUV, fragTexCoord, 0.70);
    }

    vec3 refractedScene;
    if (u_qualityTier >= 2) {
        vec2 dispersion = refractOffset * mix(0.018, 0.045, 1.0 - ndv);
        vec2 refractUVRed = clamp(refractUV + dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        vec2 refractUVBlue = clamp(refractUV - dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        refractedScene.r = texture(u_sceneTex, refractUVRed).r;
        refractedScene.g = texture(u_sceneTex, refractUV).g;
        refractedScene.b = texture(u_sceneTex, refractUVBlue).b;
    } else {
        refractedScene = texture(u_sceneTex, refractUV).rgb;
    }

    /* The caustic projection that used to live here is GONE, and this note is
     * why it must not come back in this form.
     *
     * It added `sin(x + sin(y)) * sin(y + sin(x))` raised to the third power
     * straight into refractedScene. A sin*sin lattice cubed IS a set of thin
     * bright lines — and because its frequency was scaled by the water's height
     * above the receiver, those lines bent and re-spaced across the body. That
     * is the topographic-contour banding that survived four wrong diagnoses
     * (filter continuity, iterated feedback, the wave perturbation's missing
     * tangent projection, the integer filter radius). Isolating it took a
     * debug view: views 1 and 3 proved the surface and its shading were clean,
     * 9 proved the scene copy was pixel-exact, 7 proved the validity test never
     * fired — and 11 showed the pattern alone, unmistakably.
     *
     * It is also in the wrong place physically. A caustic is light focused ONTO
     * the receiver; it belongs on the ground under the water, where it stays put
     * as the water moves. Added into the water's own refracted colour it becomes
     * a decal stuck to the liquid, sliding with the surface — which is exactly
     * why it read as contour lines rather than as a pool of light. If caustics
     * are wanted, they go on the receiver (the decal system already draws on
     * ground), not into this term. */

    // Beer-Lambert Volumetric Absorption
    float materialPeak = max(max(materialBody.r, materialBody.g), max(materialBody.b, 0.001));
    vec3 materialTransmission = clamp(materialBody / materialPeak, vec3(0.035), vec3(1.0));
    /* Beer-Lambert with a real path length, so the coefficient is a statement
     * rather than a dial: one FLUID_REFERENCE_DEPTH_M of this liquid transmits
     * exactly materialTransmission. Everything thinner is proportionally
     * clearer, which is what makes a rim read as a rim.
     *
     * The old 2.60 multiplied a thickness that had already been squeezed
     * through a saturating knee, so its value meant nothing on its own and had
     * to be re-tuned whenever the knee moved. */
    /* `extraOpacityPerM` is the liquid's own grey extinction on top of its
     * colour, and it is what separates a hot liquid from orange glass. Lava's
     * body colour is saturated, so the RED channel of materialTransmission is
     * 1.0 by construction and the background reads straight through the middle
     * of the body — the colour alone cannot make a liquid opaque. Water keeps
     * FLUID_TURBIDITY_PER_M and is unchanged. */
    vec3 absorption = -log(materialTransmission) / FLUID_REFERENCE_DEPTH_M
                    + vec3(FLUID_TURBIDITY_PER_M + extraOpacityPerM);
    vec3 transmittance = exp(-absorption * opticalPath);

    float hemi = clamp(worldNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(u_groundAmbient, u_skyAmbient, hemi);
    vec3 L = normalize(u_sunDirectionView);
    float ndl = max(dot(N, L), 0.0);

    vec3 waterScatterColor = materialBody;
    vec3 illumination = ambient + u_sunColor * (0.12 + 0.30 * ndl);
    vec3 transmitted = refractedScene * transmittance;
    float illuminationEnergy = dot(illumination, vec3(0.2126, 0.7152, 0.0722));
    float backgroundScatter = 1.0 - dot(transmittance, vec3(0.2126, 0.7152, 0.0722));
    float scatterAmount = clamp(0.30 + 0.70 * backgroundScatter, 0.0, 1.0);
    // Grazing modulation: edge-on water scatters more (long apparent optical
    // path) while the centre stays clearer so the background reads through.
    // Uniform body fill is what makes a water sphere read as a plastic ball.
    float grazingWeight = mix(0.32, 1.0, pow(1.0 - ndv, 1.5));
    vec3 inScatter = waterScatterColor * scatterAmount * (0.34 + illuminationEnergy * 0.42) * grazingWeight;
    /* A molten body does not scatter ambient light in any amount that matters —
     * it makes its own. Leaving the dielectric in-scatter at full strength put a
     * floor of lit crimson under the crust (measured at (188,88,55)) and cost
     * the plates most of their contrast against the seams. */
    if (liquidClass == FLUID_CLASS_EMISSIVE) inScatter *= 0.35;

    vec3 dielectricF0 = vec3(IorToF0(materialIor));
    vec3 fresnel = FresnelSchlick(ndv, dielectricF0);
    float volumeWeight = smoothstep(0.016, 0.095, kernelThickness);
    vec3 visibleFresnel = fresnel * mix(0.045, 1.0, volumeWeight);
    // Fresnel rim: keeps the silhouette legible on any background (bright
    // grass included) without painting a cartoon outline - it fades to zero
    // on thin edges and is lit by the sun.
    float rimStrength = pow(1.0 - ndv, 2.0) * smoothstep(0.012, 0.10, kernelThickness);
    /* Zero on a conductor. This is a DIELECTRIC's grazing-edge hack — it exists
     * so a 2%-reflective body keeps a legible silhouette against a bright
     * background. A conductor's edge is already the strongest thing on it via
     * the coloured Fresnel below, so adding this on top double-counts the rim
     * AND paints it the wrong colour: METAL's glow is a blue (120,200,255), and
     * it drew a blue fringe around the liquid-metal blob. */
    float rimGate = (liquidClass == FLUID_CLASS_CONDUCTOR) ? 0.0 : 1.0;
    vec3 rimLight = materialGlow * rimStrength * (0.12 + 0.55 * illuminationEnergy)
                  * (0.30 + 0.70 * ndl) * rimGate;
    vec3 viewWorld = normalize(mat3(u_viewToWorld) * V);
    vec3 reflectedWorld = reflect(-viewWorld, worldNormal);
    bool conductor = (liquidClass == FLUID_CLASS_CONDUCTOR);

    /* --- The environment a body reflects ------------------------------------
     *
     * A DIELECTRIC needs this to be soft. It contributes a few percent under
     * Fresnel, and a hard-edged environment there would draw a visible seam
     * across water for no gain.
     *
     * A CONDUCTOR needs the opposite, and getting this wrong is what made the
     * liquid metal read as PLASTIC. Reflection is not a few percent of a
     * conductor's look — it is ALL of it. Multiply a smooth two-colour
     * hemisphere blend (nearly constant across a body) by a coloured F0 and the
     * result is a flat tinted surface, which is exactly what plastic is. The
     * only part of the metal blob that read as metal was its UNDERSIDE, and the
     * reason is visible in the render: down there the normal crosses the
     * ground/sky boundary, so it picks up hard contrast and structure.
     *
     * So the conductor gets a high dynamic range and a HARD horizon: a dark
     * floor, a bright sky, and the bright band every real environment has where
     * the two meet. Structure everywhere, not just where the body happens to
     * bend past the terminator. */
    float skyReflection = conductor ? smoothstep(-0.045, 0.045, reflectedWorld.y)
                                    : smoothstep(-0.18, 0.62, reflectedWorld.y);
    // Night arenas have a dark sky ambient: without a glow floor the grazing
    // Fresnel edge reflects near-black and the ball gets a dark glossy rim -
    // the "plastic toy" silhouette. Boost the sky side with the material glow
    // only when the actual sky is dark; bright-sky maps stay unchanged.
    float skyLuma = dot(u_skyAmbient, vec3(0.2126, 0.7152, 0.0722));
    vec3 skyReflectionColor = u_skyAmbient * 1.38 + materialGlow * 0.65 * clamp(1.0 - skyLuma, 0.0, 1.0);
    vec3 reflection;
    if (conductor) {
        float up = reflectedWorld.y;
        /* A GRADIENT, not a colour. The first attempt at this used one flat
         * value per hemisphere, which split the body into a black plate and a
         * pale plate — better than the uniform plastic it replaced, but the
         * bright half was still a featureless patch, because there was still
         * nothing to see up there. Every real sky is brightest at the horizon
         * and falls off toward the zenith, and that falloff is what puts
         * shading across the whole upper half of a mirrored body. */
        vec3 zenith    = u_skyAmbient * 1.4;
        vec3 horizonSky = u_skyAmbient * 4.4 + materialSoft * 0.10;
        vec3 skyColor  = mix(horizonSky, zenith, smoothstep(0.02, 0.80, up));
        /* The floor is dark but not dead flat, for the same reason. */
        vec3 floorColor = u_groundAmbient * mix(0.34, 0.10, smoothstep(0.0, -0.65, up));
        reflection = mix(floorColor, skyColor, skyReflection);
        /* The horizon band. Narrow and bright — this is the feature the eye
         * reads as "polished", and it is the one thing a hemisphere blend can
         * never produce. */
        float band = pow(1.0 - min(abs(up) * 7.0, 1.0), 3.0);
        reflection += materialSoft * band * 1.15;
    } else {
        reflection = mix(u_groundAmbient * 0.68, skyReflectionColor, skyReflection);
        float horizonReflection = pow(1.0 - abs(reflectedWorld.y), 3.0);
        /* An additive pastel wash. Harmless at a dielectric's few percent;
         * on a conductor it is a uniform veil over the whole body, i.e. more
         * plastic. Not applied above. */
        reflection += materialSoft * (0.045 + horizonReflection * 0.10);
    }

    // Screen Space Reflection (SSR) for High/Ultra tier
    if (u_qualityTier >= 2) {
        vec3 rayDirView = reflect(-V, N);
        vec4 ssrHit = TraceSSR(positionView, rayDirView, fluidDistance);
        if (ssrHit.a > 0.0) {
            reflection = mix(reflection, ssrHit.rgb, ssrHit.a);
        }
    }

    /* Lifts an airborne dielectric's dark underside so it does not read as a
     * hole. A conductor's dark underside is CORRECT — it is reflecting an unlit
     * floor — and lifting it is precisely the flattening this material must
     * not have. */
    if (!conductor) {
        vec3 localReflectionFill = mix(refractedScene, u_skyAmbient, 0.30) * 0.42;
        float lowerHemisphere = 1.0 - skyReflection;
        reflection = mix(reflection, max(reflection, localReflectionFill), airborneWeight * lowerHemisphere);
    }
    vec3 dielectricBase = mix(transmitted + inScatter, reflection, visibleFresnel);

    // Specular Highlights
    //
    // The lobes below used to be razor-thin (roughness 0.035, exponents 190 and
    // 256) while the normal above carries the capillary ripple — whose shortest
    // wavelength (2*PI/53 ~= 12 cm) is smaller than one splat. A highlight that
    // narrow, swept across a rippled dome, crosses in and out of the lobe
    // several times per splat and paints CONCENTRIC RINGS on every bulge: the
    // onion pattern that showed up on the grass and bright-arena backgrounds and
    // vanished on the dark one, where there was no specular contrast to alias.
    // Widening the lobe is the fix that costs nothing: a real water surface at
    // this scale is not a mirror, and the ripple then reads as shimmer instead
    // of as contour lines.
    /* Irregular surface variation — value noise, NOT a sine of a dot product.
     *
     * `sin(dot(worldPosition, vec3(12.3, 7.1, -9.5)))` is a PLANE WAVE. Rendering
     * it on its own over the water ring showed exactly what it is: a set of
     * parallel black-and-white bands wrapped across the body. It drove three
     * things at once — the specular roughness, the sharp-glint gate and the foam
     * pattern — so those bands came out as alternating bright and dark stripes
     * with foam dashes on the light ones, which is what "like optical
     * interference fringes" means when a user reports it.
     *
     * This is the THIRD plane-wave-as-noise defect in this one file (the caustic
     * lattice and the wave perturbation's up-bias were the others), which is why
     * vnoise3/fbm3 now live in the shared noise.glsl rather than here.
     *
     * The frequency is chosen to keep the feature size the sine had — its
     * wavelength was 2*PI/|k| = 0.37 m, and one fbm3 cell at 2.7 is the same
     * 0.37 m — so only the regularity is gone, not the scale. Time translates
     * the sample point instead of phase-shifting one wave, so the field drifts
     * rather than pulsing in lockstep everywhere. */
    float surfaceNoise = fbm3(worldPosition * 2.7 + vec3(0.0, u_time * 0.35, 0.0));
    float authoredRoughness = mix(0.090, 0.150, surfaceNoise) * roughnessScale;
    float roughness = NormalFilteredRoughness(N, authoredRoughness);
    float lobeScale = BlinnExponentScale(authoredRoughness, roughness);
    vec3 sunHalf = normalize(V + L);
    float sunNdh = max(dot(N, sunHalf), 0.0);
    /* Widening a lobe must not BRIGHTEN it. GGX above is normalized and dims
     * itself; these two Blinn lobes are not, so spreading them over a larger
     * solid angle at a fixed amplitude adds energy — the first attempt at this
     * fix removed the needle and left a bigger, brighter comma in its place.
     * A pow(cos, n) lobe's solid angle goes as 1/(n+2), so the amplitude has to
     * fall with (n+2) for the widening to redistribute rather than add. */
    float broadExponent = 48.0 * lobeScale;
    float glintExponent = 96.0 * lobeScale;
    float broadSunLobe = pow(sunNdh, broadExponent) * 0.020 * ((broadExponent + 2.0) / 50.0);
    float sharpGlint = pow(sunNdh, glintExponent) * 0.22 * ((glintExponent + 2.0) / 98.0)
                     * smoothstep(0.55, 0.86, surfaceNoise);
    /* The specular F0. For a dielectric this is the same scalar the Fresnel rim
     * uses; the conductor branch below overrides it with a COLOURED F0, which is
     * the whole difference between a wet look and a metal one. */
    vec3 specularF0 = dielectricF0;
    /* The two Blinn lobes below are an un-normalized hand-rolled glitter hack
     * authored against water's 0.02 F0. A conductor's F0 is ~40x that, so
     * scaling them by it would blow out; and it does not need them — the GGX
     * term alone is already 45x brighter once F0 is a metal's. So they stay
     * exactly as they were for dielectrics and switch off for a conductor. */
    float blinnGate = 1.0;
    if (liquidClass == FLUID_CLASS_CONDUCTOR) { specularF0 = materialBody; blinnGate = 0.0; }
    vec3 specular = u_sunColor * (WaterSpecularBRDF(N, V, L, roughness, specularF0)
                                  + vec3((broadSunLobe + sharpGlint) * blinnGate)) * ndl;
    specular *= mix(vec3(1.0), materialGlow, 0.08);
    /* The cell-hash sparkle that used to live here is GONE. It took the floor of
     * the world position scaled by 90, quantising the surface into 1.1 cm CUBES,
     * and painted each selected cell a flat block of sun colour — so the "sub-centimetre glint" it
     * promised renders as axis-aligned SQUARE DOTS scattered over the body,
     * which is what the sandbox showed. A glint is a point, and a point cannot
     * be produced by a function that is constant across a cell.
     *
     * Its hash was `fract(sin(dot(...)))` as well, which ENGINE_LANDMINES #4
     * records as dying outright on Mali at large domains — so this term was
     * also wrong on every Android device. If micro-facet flicker is wanted
     * back, it has to come from a smooth function with a round falloff, never
     * from a cell index. */

    for (int i = 0; i < FLUID_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 toLight = u_pointLightPosRadius[i].xyz - positionView;
        float distanceToLight = length(toLight);
        float attenuation = clamp(1.0 - distanceToLight / max(u_pointLightPosRadius[i].w, 0.001), 0.0, 1.0);
        attenuation *= attenuation;
        if (attenuation <= 0.0) continue;
        vec3 pointL = toLight / max(distanceToLight, 0.001);
        float pointNdl = max(dot(N, pointL), 0.0);
        vec3 pointHalf = normalize(V + pointL);
        float pointExponent = 44.0 * lobeScale;
        float broadPointLobe = pow(max(dot(N, pointHalf), 0.0), pointExponent)
                             * 0.070 * ((pointExponent + 2.0) / 46.0);
        specular += u_pointLightColor[i].rgb * (WaterSpecularBRDF(N, V, pointL, roughness, specularF0) * 1.12
                                                + vec3(broadPointLobe * blinnGate)) * pointNdl * attenuation * 1.55;
    }

    // Shoreline Wetness & Edge Softening Foam
    float thicknessL = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(u_texel.x, 0.0)).r);
    float thicknessR = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(u_texel.x, 0.0)).r);
    float thicknessD = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(0.0, u_texel.y)).r);
    float thicknessU = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(0.0, u_texel.y)).r);
    float thicknessGradient = length(vec2(thicknessR - thicknessL, thicknessU - thicknessD)) * 0.5;
    
    float surfaceLaplacian = abs(-posL.z + -posR.z + -posD.z + -posU.z - 4.0 * (-positionView.z));
    float curvature = smoothstep(0.0012, 0.010, surfaceLaplacian);
    float receiverProximity = 1.0 - smoothstep(0.010, 0.070, sceneGap);
    float shoreline = smoothstep(0.004, 0.030, thicknessGradient) * receiverProximity * smoothstep(0.018, 0.080, kernelThickness);
    float crest = curvature * smoothstep(0.025, 0.085, kernelThickness) * (1.0 - smoothstep(0.16, 0.30, kernelThickness)) * 0.55 * mix(1.0, 0.16, airborneWeight);
    float foamPattern = smoothstep(0.48, 0.76, surfaceNoise);
    float foamMask = clamp(max(shoreline, crest) * foamPattern, 0.0, 0.72);
    vec3 foamColor = mix(materialSoft, vec3(1.0), 0.20);
    vec3 foam = foamColor * foamMask * (0.38 + 0.62 * dot(ambient, vec3(0.333333)))
              * (liquidClass == FLUID_CLASS_EMISSIVE ? 0.0 : crustGain);

    /* --- Thermal emission ----------------------------------------------------
     *
     * The composite had NO emission term of any kind, which is why a hot liquid
     * could only ever be reached by authoring a bright body colour — i.e. by
     * lying about its albedo, which then also brightened its in-scatter, its rim
     * and its foam.
     *
     * Emission saturates along the view path exactly like absorption does, so it
     * gets the same measured `opticalPath` in metres. The extinction is stated
     * against the same reference depth every other optical constant in this file
     * uses: one FLUID_REFERENCE_DEPTH_M of an emissive liquid reaches 1-e^-2, so
     * a body as deep as an authored one glows at ~86% and everything thinner is
     * proportionally cooler. That is the "thick = glowing core, thin = cooled
     * crust" the material wants, and it is self-limiting, so a grazing pixel
     * cannot run away to an arbitrary brightness.
     *
     * The colour is NOT Kirchhoff's law. Tying emitted colour to the absorption
     * spectrum (emissivity = absorptivity) is right for a body in thermal
     * equilibrium and wrong here: lava's absorption peaks in blue, so a
     * Kirchhoff term renders CYAN lava. A hot liquid radiates a blackbody
     * spectrum set by its temperature, which the material authors directly —
     * body for the cooler skin, glow for the core. */
    vec3 emission = vec3(0.0);
    float crustMask = 0.0;
    if (liquidClass == FLUID_CLASS_EMISSIVE && emissionStrength > 0.0) {
        /* One reference depth of an emissive liquid emits HALF its source
         * radiance, hence ln(2)/0.20. The first attempt stated "86% at one
         * reference depth" (2/0.20), which saturates the term by 0.3 m — and an
         * authored body is 0.3-0.6 m through the middle, so the whole thing sat
         * at full emission with no gradient at all and clipped to white. The
         * subtraction view (`water - emission`) showed a rich crimson body
         * underneath the entire time; only the emission was washing it out. */
        float emissionDepth = 1.0 - exp(-(0.693147 / FLUID_REFERENCE_DEPTH_M) * opticalPath);
        /* Crust, not foam. The same patchy field the foam used, read the other
         * way round: a hot liquid's skin is where it has COOLED.
         *
         * Its OWN frequency, not surfaceNoise's. surfaceNoise runs at 2.7, i.e.
         * a 0.37 m cell, which is most of an authored body — so the crust came
         * out as one smudge instead of a skin. Two octaves: 7.0 for the plates
         * (0.14 m) and 19.0 for the ragged edges between them, because a
         * single smooth octave gives round blobs and real crust breaks in
         * jagged sheets. */
        float crustPlates = fbm3(worldPosition * 7.0 + vec3(0.0, u_time * 0.18, 0.0));
        float crustDetail = fbm3(worldPosition * 19.0 - vec3(0.0, u_time * 0.11, 0.0));
        /* Narrow enough that the plates have edges, wide enough to keep the
         * mid-tones. At 0.36-0.60 the plates are soft blobs and the body reads
         * as a fireball; at 0.42-0.53 they are hard-edged patches with nothing
         * in between and it reads as cracked eggshell. */
        crustMask = smoothstep(0.40, 0.57, crustPlates * 0.78 + crustDetail * 0.22) * crustGain;

        /* Crust drives TEMPERATURE, not just brightness.
         *
         * Multiplying the finished emission by 0.16 under the crust was not
         * enough: the un-crusted 60% of the body was still at full core
         * temperature, so it read as one uniform sheet of molten gold with a
         * few dark smudges. Cooling the crust FIRST and deriving the colour
         * from the result is what produces the thing lava actually looks like —
         * dark plates with incandescent cracks between them, because a cooled
         * patch is not merely dimmer, it is REDDER. */
        float heat = emissionDepth * (1.0 - 0.92 * crustMask);
        /* The knee sits high so the MID of a body stays red-orange and only the
         * hottest part climbs to the incandescent core colour. Dropped to 0.30
         * the whole body reached core and read as molten gold. */
        float core = smoothstep(0.48, 0.94, heat);
        vec3 emissionColor = mix(materialBody, materialGlow, core);
        emission = emissionColor * heat * emissionStrength;
    }

    /* A conductor has no transmission and no volume: `dielectricBase` mixes
     * transmitted background and in-scatter under a 2% Fresnel, which is the one
     * assembly that must not survive for metal. Reflection is the whole surface,
     * modulated by the same coloured F0 the specular lobe uses. */
    vec3 base = dielectricBase;
    if (liquidClass == FLUID_CLASS_CONDUCTOR) {
        vec3 conductorFresnel = FresnelSchlick(ndv, specularF0);
        base = reflection * conductorFresnel;
    }

    /* Cooled crust is ROCK. Leaving it glossy put a wet sheen straight across
     * the plates and undid the structure the crust exists to create. */
    if (liquidClass == FLUID_CLASS_EMISSIVE) specular *= (1.0 - 0.80 * crustMask);

    vec3 water = base + specular + foam + rimLight + emission;
    finalColor = vec4(water, surfaceCoverage);
}
