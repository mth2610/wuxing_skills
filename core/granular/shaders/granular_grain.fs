#version 330
#include "core/shaders/common/vfx_composite.glsl"

in vec2 v_corner;
in vec3 v_worldNormal;
in vec3 v_worldPos;
in vec3 v_localPos;
in vec4 v_shadowCoord;
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
uniform vec3 u_camPos;
uniform sampler2D u_shadowMap;
uniform float u_shadowEnabled;
uniform float u_shadowTexel;

float Granular_SampleShadow(float ndl)
{
    if (u_shadowEnabled < 0.5) return 1.0;
    vec3 proj = v_shadowCoord.xyz / max(v_shadowCoord.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float bias = max(0.0028 * (1.0 - ndl), 0.0008);
    float z = proj.z - bias;
    float res = 1.0 / max(u_shadowTexel, 1e-5);
    vec2 t = proj.xy * res - 0.5;
    vec2 f = fract(t);
    vec2 base = (floor(t) + 0.5) * u_shadowTexel;
    float s00 = step(z, texture(u_shadowMap, base).r);
    float s10 = step(z, texture(u_shadowMap, base + vec2(u_shadowTexel, 0.0)).r);
    float s01 = step(z, texture(u_shadowMap, base + vec2(0.0, u_shadowTexel)).r);
    float s11 = step(z, texture(u_shadowMap, base + vec2(u_shadowTexel, u_shadowTexel)).r);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

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
        o.morphology = 1; // Coarse gravel / river scree
        o.baseCol = vec3(0.541, 0.502, 0.455); o.shadCol = vec3(0.188, 0.173, 0.157); o.highCol = vec3(0.863, 0.839, 0.800);
        o.glintInt = 0.55; o.glintSharp = 56.0;
    } else if (matId == 2) {
        o.morphology = 1; // Loess soil clod
        o.baseCol = vec3(0.463, 0.345, 0.227); o.shadCol = vec3(0.165, 0.118, 0.078); o.highCol = vec3(0.698, 0.580, 0.424);
        o.glintInt = 0.20; o.glintSharp = 24.0;
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
    } else if (matId == 11) {
        o.morphology = 4; // Spirit jade/mineral crystal shard
        o.baseCol = vec3(0.204, 0.776, 0.541); o.shadCol = vec3(0.071, 0.298, 0.220); o.highCol = vec3(0.816, 1.000, 0.910);
        o.glintInt = 2.40; o.glintSharp = 192.0;
    } else if (matId == 12) {
        o.morphology = 5; // Forged metal blade shard
        o.baseCol = vec3(0.580, 0.635, 0.714); o.shadCol = vec3(0.165, 0.188, 0.235); o.highCol = vec3(1.000, 0.957, 0.824);
        o.glintInt = 2.85; o.glintSharp = 224.0;
    }
    return o;
}

// Asymmetrical convex angular sand-clump SDF for fine continuum (no polar harmonics)
float Granular_FineClumpSDF(vec2 q, float seed)
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

void main()
{
    if (v_life <= 0.0) discard;

    int matId = int(v_materialId + 0.5);
    GrainOptics opt = getGrainOptics(matId);
    float ao = 1.0 - 0.52 * clamp(v_contactAO, 0.0, 1.0);
    float lifeFade = smoothstep(0.0, 0.35, v_life);
    vec3 L = normalize(u_lightDir);
    vec3 V = normalize(u_camPos - v_worldPos);

    if (opt.morphology == 1) {
        // 3D Chiseled Angular Clod / Pebble (flat geometric facet shading + strata speckle)
        vec3 N = normalize(v_worldNormal);
        if (dot(N, V) < 0.0) N = -N;

        float NdotL = max(dot(N, L), 0.0);
        float sunVis = Granular_SampleShadow(NdotL);
        float skyBounce = 0.20 + 0.14 * clamp(N.y, -1.0, 1.0);
        float grainNoise = Granular_Hash21(floor(v_localPos.xz * 7.5 + v_seed * 23.0));
        float macroTone = mix(0.84, 1.16, fract(v_seed * 29.7));

        vec3 albedo = mix(opt.shadCol, opt.baseCol * macroTone * mix(0.84, 1.16, grainNoise), 0.36 + 0.64 * ao);
        vec3 litBody = albedo * (skyBounce * ao + 0.88 * NdotL * sunVis * ao);

        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), opt.glintSharp);
        litBody += opt.highCol * (spec * opt.glintInt * sunVis * (0.4 + 0.6 * ao));

        finalColor = VFX_ResolveBody(litBody, 1.0, lifeFade);
        return;
    }

    if (opt.morphology == 2) {
        // 3D Curved & Folded Botanical Leaf / Petal (wrap diffuse + backlit SSS + veins)
        vec3 N = normalize(v_worldNormal);
        if (dot(N, V) < 0.0) N = -N;

        float midrib = 1.0 - smoothstep(0.0, 0.14, abs(v_corner.y));
        float lateralVein = 1.0 - smoothstep(0.0, 0.08, abs(fract((v_corner.x * 2.2 - abs(v_corner.y) * 1.1) * 2.0) - 0.5));
        float edgeTip = smoothstep(-0.2, 0.85, v_corner.x);

        vec3 baseTone = mix(opt.shadCol, opt.baseCol, 0.45 + 0.55 * v_tiltShade);
        baseTone = mix(baseTone, opt.highCol, midrib * 0.38 + lateralVein * 0.12 + edgeTip * 0.18);

        float wrapDiff = max(0.0, (dot(N, L) + 0.55) / 1.55);
        float sunVis = Granular_SampleShadow(max(dot(N, L), 0.0));
        float backlitSSS = pow(max(dot(-L, V), 0.0), 2.5) * 0.55 * (0.30 + 0.70 * sunVis);
        vec3 litBody = baseTone * (0.38 * ao + 0.72 * wrapDiff * sunVis + backlitSSS) * v_tiltShade;

        finalColor = VFX_ResolveBody(litBody, 1.0, lifeFade);
        return;
    }

    if (opt.morphology == 3) {
        // 3D Woody Twig Segment (bark striations + faceted cylindrical lighting)
        vec3 N = normalize(v_worldNormal);
        if (dot(N, V) < 0.0) N = -N;

        float bark = Granular_Hash21(vec2(floor(v_localPos.x * 10.0), floor((v_localPos.y + v_localPos.z) * 18.0 + v_seed * 19.0)));
        float NdotL = max(dot(N, L), 0.0);
        float sunVis = Granular_SampleShadow(NdotL);
        vec3 albedo = mix(opt.shadCol, opt.baseCol * mix(0.78, 1.22, bark), 0.32 + 0.68 * ao);
        vec3 litBody = albedo * (0.30 * ao + 0.82 * NdotL * sunVis);

        finalColor = VFX_ResolveBody(litBody, 1.0, lifeFade);
        return;
    }

    if (opt.morphology == 4) {
        // 3D Hexagonal Bipyramid Ice / Spirit Crystal Shard
        vec3 N = normalize(v_worldNormal);
        if (dot(N, V) < 0.0) N = -N;

        float NoV = clamp(dot(N, V), 0.001, 1.0);
        float NdotL = max(dot(N, L), 0.0);
        float sunVis = Granular_SampleShadow(NdotL);
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), opt.glintSharp);
        float fresnel = pow(1.0 - NoV, 2.4);
        float backlit = pow(max(dot(-L, V), 0.0), 2.5) * 0.42 * (0.30 + 0.70 * sunVis);

        // Beer-Lambert internal absorption + frosted bevels
        float pathLen = mix(0.35, 1.35, 1.0 - NoV);
        vec3 beer = exp(-max(vec3(1.0) - opt.baseCol, vec3(0.08)) * pathLen * 1.15);
        float bevel = 1.0 - smoothstep(0.0, 0.22, min(v_corner.x, 1.0 - v_corner.x));

        vec3 albedo = mix(opt.shadCol, opt.baseCol, 0.45 + 0.55 * ao) * mix(beer, vec3(1.0), 0.45);
        vec3 litBody = albedo * (0.40 * ao + 0.72 * NdotL * sunVis + backlit)
                     + opt.highCol * (spec * opt.glintInt * sunVis + fresnel * 0.65 + bevel * 0.35);

        finalColor = VFX_ResolveBody(litBody, 1.0, lifeFade);
        return;
    }

    if (opt.morphology == 5) {
        // 3D Razor Diamond-Bevel Metal Shard (studio dual-key metallic F0 reflections)
        vec3 N = normalize(v_worldNormal);
        if (dot(N, V) < 0.0) N = -N;

        float NoV = clamp(dot(N, V), 0.001, 1.0);
        vec3 R = reflect(-V, N);
        vec3 L1 = normalize(vec3(0.45, 0.78, 0.42));
        vec3 L2 = normalize(vec3(-0.62, 0.35, 0.70));
        float spec1 = pow(max(dot(R, L1), 0.0), 48.0);
        float spec2 = pow(max(dot(R, L2), 0.0), 72.0);
        float skyBand = smoothstep(-0.25, 0.65, R.y);
        float fresnel = pow(1.0 - NoV, 2.8);

        float facetDiff = 0.28 + 0.72 * max(dot(N, L1), 0.0);
        vec3 envColor = mix(opt.shadCol * 0.65, opt.baseCol * 1.35 + vec3(0.18, 0.16, 0.12), skyBand);
        vec3 litBody = mix(opt.baseCol * facetDiff * 0.58, envColor, 0.62) * (0.55 + 0.45 * ao);
        litBody += opt.highCol * (spec1 * 1.45 + spec2 * 1.15) * opt.glintInt * 0.42;
        litBody += opt.highCol * fresnel * 0.65;

        finalColor = VFX_ResolveBody(litBody, 1.0, lifeFade);
        return;
    }

    // Fine Continuum (Sand, Gold-Iron Sand, Volcanic Ash, Snow Powder)
    float dist = Granular_FineClumpSDF(v_corner, v_seed);
    if (dist > 0.0) discard;

    float centerDepth = clamp(-dist * 1.45, 0.0, 1.0);
    float z = sqrt(max(0.0, 1.0 - (1.0 - centerDepth) * (1.0 - centerDepth)));
    vec3 N = normalize(vec3(v_corner.x * 0.85, v_corner.y * 0.85, z * 1.15));

    vec2 grainGrid = floor((v_corner + vec2(v_seed * 17.0, v_seed * 31.0)) * 7.5);
    float microSpeckle = Granular_Hash21(grainGrid);
    vec3 microTilt = vec3(microSpeckle - 0.5, Granular_Hash21(grainGrid + 7.3) - 0.5, 0.0) * 0.42;
    N = normalize(N + microTilt);

    float NdotL = max(dot(N, L), 0.0);
    float sunVis = Granular_SampleShadow(NdotL);
    float macroTone = mix(0.86, 1.14, fract(v_seed * 29.7));
    float speckleTone = mix(0.82, 1.18, microSpeckle);

    vec3 albedo = mix(opt.shadCol, opt.baseCol * macroTone * speckleTone, 0.42 + 0.58 * ao);
    vec3 litBody = albedo * (0.56 * ao + 0.62 * NdotL * sunVis * ao);

    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float glintMask = step(0.86, microSpeckle) * step(0.52, fract(v_seed * 53.1 + microSpeckle));
    float spec = pow(max(dot(N, H), 0.0), max(opt.glintSharp, 24.0));
    litBody += opt.highCol * (spec * opt.glintInt * glintMask * sunVis * (0.4 + 0.6 * ao));

    float coverage = smoothstep(0.0, -0.32, dist) * 0.84 * lifeFade;
    finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
}
