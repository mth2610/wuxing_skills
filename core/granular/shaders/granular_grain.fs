#version 330
#include "core/shaders/common/vfx_composite.glsl"

in vec2 v_corner;
in float v_life;
in float v_seed;
in float v_contactAO;
in float v_materialId;
in float v_speed;

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

// Asymmetrical convex angular pebble / sand-clod signed distance (no polar flower harmonics)
float Granular_PebbleSDF(vec2 q, float seed)
{
    float s1 = fract(seed * 13.73 + 0.19);
    float s2 = fract(seed * 29.41 + 0.53);
    float s3 = fract(seed * 47.89 + 0.81);
    float s4 = fract(seed * 71.17 + 0.37);

    // Squash & rotate per grain for natural irregular proportions
    float a0 = seed * 6.2831853;
    float ca = cos(a0), sa = sin(a0);
    vec2 p = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
    p.x *= mix(0.85, 1.25, s1);
    p.y *= mix(0.85, 1.25, s2);

    // 4 non-harmonic cleavage plane cuts
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

    bool isFineSand = (v_materialId < 0.5) || (v_materialId > 2.5);
    float dist = Granular_PebbleSDF(v_corner, v_seed);
    if (dist > 0.0) discard;

    // 3D normal from convex clod slope + subtle flat cleavage facet
    float centerDepth = clamp(-dist * 1.45, 0.0, 1.0);
    float z = sqrt(max(0.0, 1.0 - (1.0 - centerDepth) * (1.0 - centerDepth)));
    vec3 N = normalize(vec3(v_corner.x * 0.85, v_corner.y * 0.85, z * 1.15));

    // Fine sand micro-speckle: breaks each splat into dozens of fine sand grains
    // so overlapping splats read as a dense granular dune/wave, not dots.
    vec2 grainGrid = floor((v_corner + vec2(v_seed * 17.0, v_seed * 31.0)) * (isFineSand ? 7.5 : 3.0));
    float microSpeckle = Granular_Hash21(grainGrid);
    vec3 microTilt = vec3(microSpeckle - 0.5, Granular_Hash21(grainGrid + 7.3) - 0.5, 0.0) * (isFineSand ? 0.42 : 0.16);
    N = normalize(N + microTilt);

    vec3 L = normalize(u_lightDir);
    vec3 V = vec3(0.0, 0.0, 1.0);

    float NdotL = max(dot(N, L), 0.0);
    float ao = 1.0 - 0.56 * clamp(v_contactAO, 0.0, 1.0);
    float macroTone = mix(0.86, 1.14, fract(v_seed * 29.7));
    float speckleTone = mix(0.82, 1.18, microSpeckle);

    vec3 albedo = mix(u_shadowColor, u_baseColor * macroTone * speckleTone, 0.42 + 0.58 * ao);
    float ambientFloor = isFineSand ? 0.56 : 0.32;
    float diffuseGain  = isFineSand ? 0.62 : 0.84;
    vec3 litBody = albedo * (ambientFloor * ao + diffuseGain * NdotL * ao);

    // Stochastic quartz / gold / mica micro-glints on surface grains
    vec3 H = normalize(L + V);
    float glintMask = step(0.88, microSpeckle) * step(0.55, fract(v_seed * 53.1 + microSpeckle));
    float spec = pow(max(dot(N, H), 0.0), max(u_glintSharpness, 24.0));
    litBody += u_highlightColor * (spec * u_glintIntensity * glintMask * (0.4 + 0.6 * ao));

    float lifeFade = smoothstep(0.0, 0.35, v_life);
    float softEdge = isFineSand ? smoothstep(0.0, -0.32, dist) * 0.82 : smoothstep(0.0, -0.10, dist);
    float coverage = softEdge * lifeFade;

    finalColor = VFX_ResolveBody(litBody, 1.0, coverage);
}
