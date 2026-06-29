#version 330

// ---------------------------------------------------------------------------
//  tube.fs — Helix tube fragment shader
//
//  Visual recipe:
//    · Scrolling layered caustic noise along tube length (V axis)
//    · Fresnel rim glow at silhouette edges
//    · Wrap-lighting (NdotL) preserves 3-D curvature — avoids flat neon
//    · Breathing emissive multiplier (spec §6)
//    · Noise-based dissolve erosion for clean FADE transition
//    · No texture sampler needed — fully procedural
//
//  Auto-bound by SkillManager_BeginShader: u_time, u_viewPos, u_resolution
//  Manually set per-draw:                  u_dissolve, u_color
// ---------------------------------------------------------------------------

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform vec3  u_viewPos;
uniform float u_time;
uniform float u_dissolve;   // 0 = fully visible, 1 = fully eroded
uniform vec4  u_color;      // rgba, driven by ELEMENT_COLOR_WATER

out vec4 finalColor;

// ---------------------------------------------------------------------------
//  NOISE UTILITIES
// ---------------------------------------------------------------------------
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);          // smooth-step blend
    return mix(
        mix(hash(i),               hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// Three-octave caustic noise — organic, not TV-static
// Key: low X multiplier so it stretches along tube length
float causticNoise(vec2 uv, float t)
{
    float n  = valueNoise(uv * vec2(1.1, 3.2) + vec2(t * 0.85,  t * 0.50)) * 0.55;
          n += valueNoise(uv * vec2(2.8, 1.4) + vec2(t * 0.55, -t * 0.75)) * 0.30;
          n += valueNoise(uv * vec2(4.5, 4.5) - vec2(t * 0.20,  0.0      )) * 0.15;
    return n;
}

// ---------------------------------------------------------------------------
//  MAIN
// ---------------------------------------------------------------------------
void main()
{
    // ---- Scrolling UV: water flows forward along tube (V coord) ----
    vec2 uvFlow = vec2(fragTexCoord.x,
                       fragTexCoord.y - u_time * 1.35);

    float caustic = causticNoise(uvFlow, u_time);

    // ---- Noise-based dissolve erosion (spec §7 — sweep 0→1 on fade) ----
    float dNoise = valueNoise(fragTexCoord * 7.5 + vec2(u_time * 0.28, 0.0));
    if (dNoise < u_dissolve * 1.3) discard;

    // ---- Fresnel rim (view-dependent silhouette glow) ----
    vec3  viewDir = normalize(u_viewPos - fragPosition);
    float NdotV   = clamp(dot(fragNormal, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - NdotV, 2.8);

    // ---- Wrap lighting — preserves 3-D volume (spec §7) ----
    //      NdotL remapped to [0,1] so dark side gets 50% rather than 0%
    vec3  lightDir = normalize(vec3(0.25, 1.0, 0.30));
    float NdotL    = dot(fragNormal, lightDir) * 0.5 + 0.5;

    // ---- Breathing emissive multiplier (spec §6) ----
    float breathe  = 1.35 + 0.10 * sin(u_time * 3.5);

    // ---- Color assembly ----
    vec3 baseColor    = u_color.rgb;

    // Caustic highlights: mix toward icy white where wave peaks
    vec3 causticColor = mix(baseColor, vec3(0.80, 0.95, 1.00), caustic * 0.44);

    // Fresnel rim: push toward pure white at silhouette edges
    // Restrict glow to ≤30% of surface so the mesh reads as 3-D (spec §7)
    float glowMask = smoothstep(0.30, 0.80, fresnel);
    vec3 rimColor  = mix(causticColor, vec3(0.90, 0.98, 1.00), glowMask * 0.42);

    // Wrap-lit final colour
    vec3 litColor  = rimColor * NdotL * breathe;

    // ---- Alpha ----
    // Smooth edge at dissolve boundary to avoid hard pixel cuts
    float edgeFade = smoothstep(u_dissolve * 1.3, u_dissolve * 1.3 + 0.07, dNoise);
    float alpha    = u_color.a * edgeFade * (0.78 + 0.22 * fresnel);

    finalColor = vec4(litColor, alpha);
}
