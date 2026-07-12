#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"

// ============================================================
// Water Splash Material (Fragment Shader)
// Special shader simulating "Sheet Instability" and tearing 
// (màng nước rách) for liquid splashes using customParam1 
// as the lifetime progress tracker.
// ============================================================

uniform sampler2D texture1;         // water_caustics.png (noise map)
uniform int   u_hasTexture1;
uniform vec4  u_baseColor;
uniform float u_translucency;
uniform float u_dissolve;
uniform float u_rimStrength;
uniform float u_fresnelPower;
uniform float u_emissiveIntensity;
uniform float u_distortionStrength;

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)

void main() {
    vec3 normal   = normalize(fragNormal);
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    float diffuse = calcDiffuse(normal, lightDir, 0.2);
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    vec3 baseColor = u_baseColor.rgb * diffuse;
    
    // Read the noise from the caustics map
    float noiseVal = 1.0;
    if (u_hasTexture1 != 0) {
        // Use regular UV mapping so the water caustics stretch properly with the mesh
        // Scale UV.x by 4.0 to repeat around the circumference of the splash
        noiseVal = texture(texture1, fragTexCoord * vec2(4.0, 1.0)).r;
        baseColor = baseColor * mix(0.5, 2.0, noiseVal);
    }

    baseColor += baseColor * u_emissiveIntensity;

    float lightFacing = max(dot(normal, lightDir), 0.0);
    float rim = fresnel * mix(0.3, 1.0, lightFacing);
    baseColor += u_baseColor.rgb * rim * u_rimStrength;

    // --- SHEET INSTABILITY & TEARING (Màng nước vỡ) ---
    float progress = u_customParam1;
    
    // Thickness decreases as the splash expands (progress approaches 1.0).
    // Initial variations in thickness are provided by the noise texture.
    float thickness = noiseVal * (1.0 - progress * 0.85); 
    
    // Threshold increases over time. Puncture occurs when thickness < threshold.
    // We start tearing aggressively around progress = 0.3.
    float threshold = smoothstep(0.25, 1.0, progress) * 0.65; 
    
    float tearFactor = thickness - threshold;
    
    if (tearFactor < 0.0) {
        // Hole torn! Fragment is discarded.
        discard;
    }
    
    float glassAlpha = mix(0.3, 0.9, fresnel);
    float alpha = mix(u_baseColor.a, glassAlpha, u_translucency);
    
    // Surface Tension Froth (bọt nước ở mép rách)
    // If the pixel is very close to the tearing edge, it glows bright white 
    // and becomes opaque, simulating the accumulation of liquid via surface tension.
    float frothWidth = 0.06;
    if (tearFactor < frothWidth) {
        float edgeGlow = smoothstep(frothWidth, 0.0, tearFactor); // 1.0 exactly on edge, 0.0 inward
        vec3 frothColor = vec3(1.0, 1.0, 1.0);
        baseColor = mix(baseColor, frothColor, edgeGlow * 0.9);
        alpha = mix(alpha, 1.0, edgeGlow);
    }

    // Global fade out at the very end of the lifespan to prevent sudden pops
    alpha *= (1.0 - smoothstep(0.85, 1.0, progress));

    finalColor = vec4(baseColor, alpha);
}
