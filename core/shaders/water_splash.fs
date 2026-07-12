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

uniform sampler2D texture0;         // water_caustics.png (noise map, bound by Raylib)
uniform vec4  u_baseColor;          // Pushed by Material_Begin
uniform float u_translucency;
uniform float u_dissolve;
uniform float u_rimStrength;
uniform float u_fresnelPower;
uniform float u_emissiveIntensity;
uniform float u_distortionStrength;

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 viewDir = normalize(cameraPos - fragPosition);
    vec3 normal = normalize(fragNormal);

    float diffuse = calcDiffuse(normal, lightDir, 0.2);
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    vec3 baseColor = u_baseColor.rgb * diffuse;
    
    // Pure water looks best with minimal emissive, mostly relying on Fresnel edges
    baseColor += baseColor * u_emissiveIntensity;
    
    // Add rim lighting (Fresnel) for the specular water edge look
    // Tint fresnel with cyan for water!
    baseColor += vec3(fresnel) * u_rimStrength * u_baseColor.rgb; 
    
    float alpha = u_baseColor.a * u_translucency;
    FS_FinalOutput(vec4(baseColor, alpha));
}
