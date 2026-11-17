#version 330
#include "core/shaders/common/vfx_composite.glsl"

in vec2 v_corner;
in float v_life;
in float v_seed;
in float v_contactAO;
in float v_materialId;
in float v_speed;
in float v_flags;
in float v_tiltShade;

out vec4 finalColor;

uniform vec3 u_baseColor;
uniform vec3 u_shadowColor;
uniform vec3 u_highlightColor;
uniform float u_glintIntensity;
uniform float u_glintSharpness;
uniform vec3 u_lightDir;

// Sine-free 2D hash (Mali-safe per ENGINE_LANDMINES)
float Granular_Hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

struct GrainOptics {
    int morphology;
    vec3 baseCol;
    vec3 shadCol;
    vec3 highCol;
    float glintInt;
    float glintSharp;
};

GrainOptics getGrainOptics(int matId)
{
    GrainOptics o;
    o.morphology = 0;
    o.baseCol = u_baseColor;
    o.shadCol = u_shadowColor;
    o.highCol = u_highlightColor;
    o.glintInt = u_glintIntensity;
    o.glintSharp = u_glintSharpness;

    if (matId == 0) {
        o.baseCol = vec3(0.910, 0.776, 0.533); o.shadCol = vec3(0.580, 0.447, 0.282); o.highCol = vec3(1.000, 0.973, 0.878);
        o.glintInt = 1.45; o.glintSharp = 128.0;
    } else if (matId == 1) {
        o.morphology = 1;
        o.baseCol = vec3(0.541, 0.502, 0.455); o.shadCol = vec3(0.188, 0.173, 0.157); o.highCol = vec3(0.863, 0.839, 0.800);
        o.glintInt = 0.45; o.glintSharp = 48.0;
    } else if (matId == 2) {
        o.morphology = 1;
        o.baseCol = vec3(0.463, 0.345, 0.227); o.shadCol = vec3(0.165, 0.118, 0.078); o.highCol = vec3(0.698, 0.580, 0.424);
        o.glintInt = 0.15; o.glintSharp = 24.0;
    } else if (matId == 3) {
        o.baseCol = vec3(0.776, 0.604, 0.227); o.shadCol = vec3(0.282, 0.188, 0.063); o.highCol = vec3(1.000, 0.910, 0.510);
        o.glintInt = 2.10; o.glintSharp = 160.0;
    } else if (matId == 4) {
        o.baseCol = vec3(0.267, 0.243, 0.235); o.shadCol = vec3(0.086, 0.071, 0.071); o.highCol = vec3(1.000, 0.510, 0.188);
        o.glintInt = 0.85; o.glintSharp = 64.0;
    } else if (matId == 5) {
        o.morphology = 2; // Dry autumn leaf
        vec3 autumnA = vec3(0.780, 0.430, 0.145);
        vec3 autumnB = vec3(0.840, 0.620, 0.180);
        vec3 autumnC = vec3(0.660, 0.240, 0.110);
        float s = fract(v_seed * 19.3);
        o.baseCol = s < 0.5 ? mix(autumnA, autumnB, s * 2.0) : mix(autumnB, autumnC, (s - 0.5) * 2.0);
        o.shadCol = vec3(0.345, 0.180, 0.071);
        o.highCol = vec3(0.961, 0.776, 0.361);
        o.glintInt = 0.35; o.glintSharp = 24.0;
    } else if (matId == 6) {
        o.morphology = 2; // Fresh emerald spirit leaf
        o.baseCol = mix(vec3(0.180, 0.620, 0.260), vec3(0.320, 0.740, 0.340), fract(v_seed * 13.7));
        o.shadCol = vec3(0.086, 0.267, 0.125);
        o.highCol = vec3(0.706, 0.961, 0.510);
        o.glintInt = 0.55; o.glintSharp = 36.0;
    } else if (matId == 7) {
        o.morphology = 3; // Dry twig
        o.baseCol = vec3(0.424, 0.298, 0.188); o.shadCol = vec3(0.157, 0.102, 0.063); o.highCol = vec3(0.651, 0.502, 0.353);
        o.glintInt = 0.18; o.glintSharp = 18.0;
    } else if (matId == 8) {
        o.morphology = 2; // Flower petal
        vec3 petalRose = vec3(1.000, 0.659, 0.792);
        vec3 petalPearl = vec3(1.000, 0.910, 0.945);
        o.baseCol = mix(petalRose, petalPearl, fract(v_seed * 23.1) * 0.65);
        o.shadCol = vec3(0.647, 0.267, 0.439);
        o.highCol = vec3(1.000, 0.925, 0.965);
        o.glintInt = 0.42; o.glintSharp = 28.0;
    } else if (matId == 9) {
        o.morphology = 4; // Ice shard
        o.baseCol = vec3(0.608, 0.878, 1.000); o.shadCol = vec3(0.165, 0.376, 0.580); o.highCol = vec3(1.000, 1.000, 1.000);
        o.glintInt = 2.60; o.glintSharp = 192.0;
    } else if (matId == 10) {
        o.morphology = 0; // Snow powder
        o.baseCol = vec3(0.933, 0.965, 1.000); o.shadCol = vec3(0.502, 0.635, 0.808); o.highCol = vec3(1.000, 1.000, 1.000);
        o.glintInt = 1.75; o.glintSharp = 160.0;
    }
    return o;
}

// Asymmetrical convex angular pebble / sand-clod signed distance (no polar flower harmonics)
float Granular_PebbleSDF(vec2 q, float seed)
{
    float s1 = fract(seed * 13.73 + 0.19);
    float s2 = fract(seed * 29.41 + 0.53);
    float s3 = fract(seed * 47.89 + 0.81);
    float s4 = fract(seed * 71.17 + 0.37);

    float a0 = seed * 6.2831853;
    float ca = cos(a0), sa = sin(a0);
    vec2 p = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
    p.x *= mix(0.85, 1.25, s1);
    p.y *= mix(0.85, 1.25, s2);

    vec2 n1 = normalize(vec2(1.0, 0.35 + 0.6 * s1));
    vec2 n2 = normalize(vec2(-0.65 - 0.5 * s2, 1.0));
    vec2 n3 = normalize(vec2(-0.85, -0.55 - 0.6 * s3));
    vec2 n4 = normalize(vec2(0.45 + 0.5 * s4, -1.0));

    float d = dot(p, n1) - mix(0.68, 0.86, s1);
    d = max(d, dot(p, n2) - mix(0.66, 0.86, s2));
    d = max(d, dot(p, n3) - mix(0.68, 0.86, s3));
    d = max(d, dot(p, n4) - mix(0.66, 0.86, s4));
    return max(d, length(p) - 0.92);
}

// Acuminate botanical leaf or rounded blossom petal SDF
float Granular_LeafLaminaSDF(vec2 q, float seed, bool isPetal, out float midribDist)
{
    // Slight natural midrib curvature
    float bend = (fract(seed * 31.7) - 0.5) * 0.22;
    vec2 p = q;
    p.y -= bend * (1.0 - p.x * p.x);
    midribDist = abs(p.y);

    float t = clamp(p.x * 0.5 + 0.5, 0.0, 1.0);
    float halfWidth;
    if (isPetal) {
        // Tear-drop / lotus petal: narrow base, lush rounded tip
        halfWidth = sin(pow(t, 0.68) * 3.1415926) * 0.88;
    } else {
        // Tapered acuminate leaf blade
        halfWidth = sin(pow(t, 0.78) * 3.1415926) * (0.76 - 0.14 * t);
    }
    float dx = abs(p.x) - 0.92;
    float dy = abs(p.y) - max(halfWidth, 0.01);
    return max(dx, dy);
}

// Anisotropic woody twig segment with side knot
float Granular_TwigSDF(vec2 q, float seed, out float barkGrain)
{
    float curve = (fract(seed * 17.3) - 0.5) * 0.18;
    vec2 p = q;
    p.y -= curve * (p.x * p.x - 0.35);
    float dMain = max(abs(p.x) - 0.90, abs(p.y) - mix(0.18, 0.25, fract(seed * 43.1)));

    // Small forked stub on one side
    float stubX = (fract(seed * 53.7) - 0.5) * 0.55;
    vec2 sVec = vec2(p.x - stubX, p.y - 0.16);
    vec2 sDir = normalize(vec2(0.7, 0.7));
    float proj = clamp(dot(sVec, sDir), 0.0, 0.42);
    float dStub = length(sVec - sDir * proj) - 0.09;

    barkGrain = Granular_Hash21(vec2(floor(p.x * 8.0), floor(p.y * 22.0 + seed * 19.0)));
    return min(dMain, dStub);
}

// Sharp hexagonal/diamond ice crystal shard SDF
float Granular_IceShardSDF(vec2 q, float seed, out float ridge)
{
    float a0 = (fract(seed * 19.1) - 0.5) * 0.35;
    float ca = cos(a0), sa = sin(a0);
    vec2 p = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
    float d1 = abs(p.x) * 0.46 + abs(p.y) * 1.15 - 0.85;
    float d2 = abs(p.x) - 0.92;
    float d = max(d1, d2);
    ridge = 1.0 - smoothstep(0.0, 0.22, abs(p.y));
    return d;
}

void main()
{
    if (v_life <= 0.0) discard;

    int matId = int(v_materialId + 0.5);
    GrainOptics opt = getGrainOptics(matId);
    float ao = 1.0 - 0.56 * clamp(v_contactAO, 0.0, 1.0);
    float lifeFade = smoothstep(0.0, 0.35, v_life);
    vec3 L = normalize(u_lightDir);
    vec3 V = vec3(0.0, 0.0, 1.0);

    if (opt.morphology == 2) {
        // Planar lamina: leaf or flower petal
        float midribDist = 0.0;
        bool isPetal = (matId == 8);
        float dist = Granular_LeafLaminaSDF(v_corner, v_seed, isPetal, midribDist);
        if (dist > 0.0) discard;

        float vein = 1.0 - smoothstep(0.0, 0.09, midribDist);
        float edgeTip = smoothstep(-0.3, 0.8, v_corner.x);
        vec3 baseTone = mix(opt.shadCol, opt.baseCol, 0.45 + 0.55 * v_tiltShade);
        baseTone = mix(baseTone, opt.highCol, vein * 0.38 + edgeTip * 0.18);

        // Dual-sided wrap SSS lighting
        vec3 N = normalize(vec3(v_corner.y * 0.35, 0.25, 0.90));
        float wrapDiff = max(0.0, (dot(N, L) + 0.45) / 1.45);
        vec3 litBody = baseTone * (0.42 * ao + 0.72 * wrapDiff) * v_tiltShade;
        float coverage = smoothstep(0.0, -0.08, dist) * lifeFade;
        finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
        return;
    }

    if (opt.morphology == 3) {
        // Elongated woody twig
        float bark = 0.5;
        float dist = Granular_TwigSDF(v_corner, v_seed, bark);
        if (dist > 0.0) discard;

        float cylZ = sqrt(max(0.0, 1.0 - clamp(v_corner.y * 3.2, -1.0, 1.0) * clamp(v_corner.y * 3.2, -1.0, 1.0)));
        vec3 N = normalize(vec3(0.0, v_corner.y * 2.5, cylZ));
        float NdotL = max(dot(N, L), 0.0);
        vec3 albedo = mix(opt.shadCol, opt.baseCol * mix(0.80, 1.18, bark), 0.35 + 0.65 * ao);
        vec3 litBody = albedo * (0.34 * ao + 0.78 * NdotL);
        float coverage = smoothstep(0.0, -0.08, dist) * lifeFade;
        finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
        return;
    }

    if (opt.morphology == 4) {
        // Faceted ice shard
        float ridge = 0.0;
        float dist = Granular_IceShardSDF(v_corner, v_seed, ridge);
        if (dist > 0.0) discard;

        vec3 N = normalize(vec3(v_corner.x * 0.45, sign(v_corner.y) * 0.65, 0.75));
        float NdotL = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), opt.glintSharp);
        float fresnel = pow(1.0 - max(N.z, 0.0), 2.2);
        vec3 albedo = mix(opt.shadCol, opt.baseCol, 0.50 + 0.50 * ao);
        vec3 litBody = albedo * (0.48 * ao + 0.68 * NdotL) + opt.highCol * (spec * opt.glintInt + ridge * 0.35 + fresnel * 0.55);
        float coverage = smoothstep(0.0, -0.06, dist) * lifeFade;
        finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
        return;
    }

    // Fine continuum (0, 3, 4, 10) or angular clod (1, 2)
    bool isFineSand = (opt.morphology == 0);
    float dist = Granular_PebbleSDF(v_corner, v_seed);
    if (dist > 0.0) discard;

    float centerDepth = clamp(-dist * 1.45, 0.0, 1.0);
    float z = sqrt(max(0.0, 1.0 - (1.0 - centerDepth) * (1.0 - centerDepth)));
    vec3 N = normalize(vec3(v_corner.x * 0.85, v_corner.y * 0.85, z * 1.15));

    vec2 grainGrid = floor((v_corner + vec2(v_seed * 17.0, v_seed * 31.0)) * (isFineSand ? 7.5 : 3.0));
    float microSpeckle = Granular_Hash21(grainGrid);
    vec3 microTilt = vec3(microSpeckle - 0.5, Granular_Hash21(grainGrid + 7.3) - 0.5, 0.0) * (isFineSand ? 0.42 : 0.16);
    N = normalize(N + microTilt);

    float NdotL = max(dot(N, L), 0.0);
    float macroTone = mix(0.86, 1.14, fract(v_seed * 29.7));
    float speckleTone = mix(0.82, 1.18, microSpeckle);

    vec3 albedo = mix(opt.shadCol, opt.baseCol * macroTone * speckleTone, 0.42 + 0.58 * ao);
    float ambientFloor = isFineSand ? 0.56 : 0.32;
    float diffuseGain  = isFineSand ? 0.62 : 0.84;
    vec3 litBody = albedo * (ambientFloor * ao + diffuseGain * NdotL * ao);

    vec3 H = normalize(L + V);
    float glintMask = step(0.88, microSpeckle) * step(0.55, fract(v_seed * 53.1 + microSpeckle));
    float spec = pow(max(dot(N, H), 0.0), max(opt.glintSharp, 24.0));
    litBody += opt.highCol * (spec * opt.glintInt * glintMask * (0.4 + 0.6 * ao));

    float softEdge = isFineSand ? smoothstep(0.0, -0.32, dist) * 0.82 : smoothstep(0.0, -0.10, dist);
    float coverage = softEdge * lifeFade;

    finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
}
