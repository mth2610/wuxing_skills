#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"

// ============================================================
// WUXING — Crystal Shader Material
// Backing shader for premium translucent/emissive crystals.
// Handles Fresnel rim, height gradient, thickness absorption,
// fake refraction, internal procedural cracks, and sparkling speculars.
// ============================================================

uniform vec4  u_baseColor;
uniform vec4  u_edgeColor;          // Color at the tip and edges
uniform float u_fresnelPower;       // default 4.0
uniform float u_rimStrength;        // default 1.0
uniform float u_refraction;         // default 0.15 (fake distortion)
uniform float u_sparkle;            // default 0.8 (sparkle threshold/amount)
uniform float u_crack;              // default 0.5 (crack noise strength)
uniform float u_emission;           // default 0.3 (emissive glow)
uniform float u_thickness;          // default 2.0 (absorption depth)
uniform float u_dissolve;           // default 0.0 (dissolve progress)

uniform sampler2D texture1;         // detail/caustics map for fake refraction

// Local 3D Value Noise utilizing hash3 from noise.glsl
float vnoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(
            mix(hash3(i + vec3(0.0, 0.0, 0.0)), hash3(i + vec3(1.0, 0.0, 0.0)), f.x),
            mix(hash3(i + vec3(0.0, 1.0, 0.0)), hash3(i + vec3(1.0, 1.0, 0.0)), f.x),
            f.y
        ),
        mix(
            mix(hash3(i + vec3(0.0, 0.0, 1.0)), hash3(i + vec3(1.0, 0.0, 1.0)), f.x),
            mix(hash3(i + vec3(0.0, 1.0, 1.0)), hash3(i + vec3(1.0, 1.0, 1.0)), f.x),
            f.y
        ),
        f.z
    );
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    // 1. Height Gradient (derived from UV coordinate fragTexCoord.y passed from vertex shader)
    float heightFactor = clamp(fragTexCoord.y, 0.0, 1.0);
    vec3 crystalBase = mix(u_baseColor.rgb, u_edgeColor.rgb, heightFactor);

    // 2. Diffuse shading (flat faceted light)
    float diffuse = calcDiffuse(normal, lightDir, 0.25);

    // 3. Fresnel (rim glow on sharp edges)
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);
    vec3 rimGlow = u_edgeColor.rgb * fresnel * u_rimStrength * 0.6;

    // 4. Fake Refraction using detail texture1 with normal-based distortion
    vec2 refractUV = fragPosition.xz * 1.5 + normal.xy * u_refraction;
    vec3 refractedColor = texture(texture1, refractUV).rgb;
    // Blend refraction with base (refracted light gets colored by the crystal base to prevent washout)
    vec3 finalColorRGB = mix(crystalBase * diffuse, refractedColor * crystalBase, u_refraction * 0.5);

    // 5. Thickness Absorption (attenuation in center, brighter on edges)
    float thicknessFactor = mix(0.3, 1.0, fresnel);
    float absorption = 1.0 - exp(-thicknessFactor * u_thickness);
    finalColorRGB = mix(finalColorRGB * 0.25, finalColorRGB, absorption);

    // 6. Internal Crack Noise (using local 3D value noise)
    if (u_crack > 0.01)
    {
        float cNoise = vnoise3D(fragPosition * 8.0);
        float crackLine = smoothstep(0.3, 0.32, cNoise) * smoothstep(0.36, 0.34, cNoise);
        finalColorRGB += u_edgeColor.rgb * crackLine * u_crack * 0.4;
    }

    // 7. Sparkling Highlights (moving speckles)
    if (u_sparkle > 0.01)
    {
        float sparkNoise = vnoise3D(vec3(fragPosition.xy * 60.0, u_time * 2.5));
        if (sparkNoise > 0.78)
        {
            float sparkleFactor = (sparkNoise - 0.78) / 0.22;
            finalColorRGB += vec3(1.5) * sparkleFactor * u_sparkle;
        }
    }

    // 8. Additive Rim and Emission Glow (calibrated to prevent color blowout to pure white)
    finalColorRGB += rimGlow;
    finalColorRGB += crystalBase * u_emission * 0.4;

    // 9. Dissolve Effect
    float alpha = mix(u_baseColor.a, 1.0, fresnel);
    if (u_dissolve > 0.0)
    {
        float dNoise = hash3(floor(normal * 30.0));
        if (dNoise < u_dissolve) discard;
        // Edge glow during dissolve
        float edge = smoothstep(u_dissolve, u_dissolve + 0.08, dNoise);
        finalColorRGB = mix(u_edgeColor.rgb * 2.5, finalColorRGB, edge);
    }

    finalColor = vec4(finalColorRGB, alpha);
}
