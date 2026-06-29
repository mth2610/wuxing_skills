#version 330

// ---------------------------------------------------------------------------
//  orb.fs — Coil-phase water orb fragment shader
//
//  Visual recipe:
//    · Animated surface turbulence (2 noise octaves, distinct axes)
//    · Fresnel rim glow — strong at silhouette, dimmer at face
//    · Wrap-lighting (NdotL) keeps 3-D sphere shape readable
//    · Breathing emissive intensifies as glowT → 1 (buildup climax)
//    · Alpha fades in proportional to glowT (orb materialises from mist)
//    · No flat neon: glow is restricted to <30% of visible surface
//
//  Auto-bound by SkillManager_BeginShader: u_time, u_viewPos, u_resolution
//  Manually set per-draw:                  u_glowT, u_color
// ---------------------------------------------------------------------------

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform vec3  u_viewPos;
uniform float u_time;
uniform float u_glowT;   // 0→1: orb materialisation progress (COIL ratio)
uniform vec4  u_color;   // rgba, driven by ELEMENT_COLOR_WATER

out vec4 finalColor;

// ---------------------------------------------------------------------------
//  NOISE UTILITIES  (identical helpers to tube.fs — each FS is self-contained)
// ---------------------------------------------------------------------------
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i),               hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// ---------------------------------------------------------------------------
//  MAIN
// ---------------------------------------------------------------------------
void main()
{
    // ---- View direction & Fresnel ----
    vec3  viewDir = normalize(u_viewPos - fragPosition);
    float NdotV   = clamp(dot(fragNormal, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - NdotV, 3.2);

    // ---- Animated surface turbulence ----
    // Two noise layers on orthogonal UV axes → churning water effect
    float n1   = valueNoise(fragTexCoord * 3.8 + vec2( u_time * 0.68,  u_time * 0.44));
    float n2   = valueNoise(fragTexCoord * 6.2 - vec2( u_time * 0.48,  u_time * 0.82));
    float surf = n1 * 0.62 + n2 * 0.38;

    // ---- Wrap lighting (spec §7 — preserves 3-D curvature) ----
    vec3  lightDir = normalize(vec3(0.30, 1.0, 0.22));
    float NdotL    = dot(fragNormal, lightDir) * 0.5 + 0.5;  // wrap → [0, 1]

    // ---- Breathing emissive (spec §6) ----
    //      Phase-offset per glowT so each orb "pulses" during buildup
    float breathe = 1.35 + 0.15 * sin(u_time * 3.5 + u_glowT * 5.2);

    // ---- Color assembly ----
    vec3 baseColor = u_color.rgb;

    // Surface turbulence + caustic sheen
    vec3 surfColor = mix(baseColor,
                         vec3(0.76, 0.95, 1.00),
                         surf * 0.28 + fresnel * 0.52);

    // Rim glow — restricted to silhouette edge (≤30% of surface, spec §7)
    float glowMask = smoothstep(0.45, 1.00, fresnel);
    vec3 rimColor  = mix(surfColor,
                         vec3(0.92, 0.99, 1.00),
                         glowMask * 0.38);

    // Wrap-lit final colour
    vec3 litColor  = rimColor * NdotL * breathe;

    // ---- Alpha ----
    // Orb fades in from 0 as glowT rises.
    // Face centre is slightly translucent; rim is denser (water-glass look).
    float alpha = u_color.a * u_glowT * (0.65 + 0.35 * (1.0 - NdotV));

    finalColor = vec4(litColor, alpha);
}
