#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

uniform float u_dissolve;

/* Value noise generator — kept local (NOT swapped for noise.glsl's
 * hash2/vnoise/fbm2): hash21() uses a fract/dot-based scramble with no
 * sin(), while noise.glsl's hash2() is sin(dot(...))*43758.5453123 — a
 * structurally different hash. Both feed a bilinear value-noise blend
 * (same interpolation shape), but the actual per-pixel field differs, and
 * this noise drives the dissolve crumble + grain pattern below, so
 * swapping it would silently change the visual result with no way to
 * screenshot-verify this session. See CLAUDE.md safety rule. */
float hash21(vec2 p)
{
    p  = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x),
               mix(c, d, u.x),
               u.y);
}

/* 2-Octave Fractal Brownian Motion */
float fbm(vec2 p)
{
    float v = 0.0;
    v += 0.65 * valueNoise(p);
    v += 0.35 * valueNoise(p * 2.3 + vec2(u_time * 0.4, 3.1));
    return v;
}

void main()
{
    /* Wood Base Color (deep jungle forest green & gnarled wood brown) */
    vec3 woodBrown = vec3(0.35, 0.28, 0.18);
    vec3 leafGreen = vec3(0.12, 0.52, 0.16);

    vec3 N = normalize(fragNormal);
    float upFactor = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);

    // Mix green and brown based on height to make it look like a sprouting plant/vine
    vec3 baseColor = mix(woodBrown, leafGreen, pow(fragTexCoord.y, 0.8));
    baseColor *= 1.3; // Brighten the base color

    // Add vertical wood grain stripes using coordinate noise
    float grain = valueNoise(vec2(fragTexCoord.x * 24.0, fragTexCoord.y * 3.0));
    baseColor = mix(baseColor, baseColor * 0.65, smoothstep(0.55, 0.7, grain));

    /* 3D Lighting — viewPos/u_lightDir now come from fs_header.glsl (real
     * environment sun direction, auto-bound by SkillManager_BeginShader)
     * instead of the hand-rolled u_camPos uniform + hardcoded light dir. */
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(u_lightDir);

    float NdotL = max(dot(N, L), 0.0);
    float fresnel = calcFresnel(N, V, 3.0);

    // Wrap diffuse — NOT swapped for calcDiffuse(): wrap diffuse
    // (NdotL*0.65+0.35, a linear remap with no hard floor) is a different
    // curve shape than calcDiffuse's max(dot, ambient) hard floor — e.g. at
    // NdotL=0.5 wrap gives 0.675 vs calcDiffuse(ambient=0.35) giving 0.5.
    float diff = NdotL * 0.65 + 0.35;

    // Rim light (greenish for wood)
    vec3 rimLight = vec3(0.2, 0.4, 0.1) * fresnel;

    vec3 diffuse = baseColor * diff + rimLight;

    /* Continuous shader rendering when intact */
    if (u_dissolve < 0.001)
    {
        // Add a gentle organic breathing glow to the edges of the green thorn and boost brightness
        float pulse = sin(u_time * 3.5) * 0.15 + 1.1;
        finalColor = vec4(diffuse * pulse, 1.0);
        return;
    }

    /* Dissolve logic: crumble from top (V = 1) down to base (V = 0) — kept
     * local (NOT swapped for fx.glsl's dissolveCalc()): this is a
     * height-biased front (heightFromTop shifts the effective threshold
     * per vertical position for a top-down crumble direction) with an
     * exponential glow falloff (exp(-threshold*9.0)), whereas dissolveCalc
     * uses a flat/uniform threshold with a fixed-width smoothstep edge band
     * — different curve shape and no directionality. Not equivalent. */
    float heightFromTop = 1.0 - fragTexCoord.y;
    float noise = fbm(fragTexCoord * 6.5 + vec2(u_time * 0.1, 0.0));

    // Dissolve front threshold
    float front = u_dissolve - heightFromTop * (1.0 - u_dissolve * 0.35);
    float threshold = noise - front;

    if (threshold < 0.0)
    {
        discard;
    }

    /* Neon Emerald green glow on crumbling boundary */
    float edgeGlow = exp(-threshold * 9.0) * u_dissolve;
    vec3 glowColor = mix(vec3(0.1, 1.0, 0.2), vec3(0.02, 0.5, 0.05), u_dissolve);

    diffuse += glowColor * edgeGlow * 3.0;

    finalColor = vec4(diffuse, 1.0);
}
