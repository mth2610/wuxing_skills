#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

uniform float u_dissolve;
uniform vec4 u_color;

void main() {
    // Create an irregular bark texture via FBM mapping over the tube's UVs
    vec2 barkUV = fragTexCoord * vec2(15.0, 2.0);
    float barkNoise = fbm2N(barkUV, 4);
    
    // Base green wood tone, darkened and mottled by bark lines
    vec3 baseColor = u_color.rgb * 0.35; 
    baseColor = mix(baseColor, u_color.rgb * 0.8, barkNoise);

    // Standard Project light direction
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    // Dissolve logic for fadeout
    float edgeFactor;
    float dNoise = hash3(floor(fragPosition * 6.0));
    if (dissolveCalc(dNoise, u_dissolve, 0.1, edgeFactor) >= 1.0) discard;
    
    // Add glowing amber/green energy edge on dissolve
    baseColor = mix(baseColor, vec3(0.5, 0.9, 0.1), edgeFactor);

    // Normal Perturbation for bark ridges
    // We use a small epsilon to compute the gradient of the bark noise field
    vec3 norm = fragNormal;
    float eps = 0.02;
    float h0 = barkNoise;
    float hu = fbm2N(barkUV + vec2(eps, 0.0), 4);
    float hv = fbm2N(barkUV + vec2(0.0, eps), 4);
    norm = perturbNormal(norm, vec2(h0 - hu, h0 - hv), 0.7);

    // Core lighting
    float diff = calcDiffuse(norm, lightDir, 0.15);
    float fres = calcFresnel(norm, normalize(viewPos - fragPosition), 4.0);

    // Specular highlight to make it look a bit wet/plant-like
    float spec = calcSpecular(norm, lightDir, normalize(viewPos - fragPosition), 32.0);

    vec3 finalRGB = baseColor * diff + vec3(0.1, 0.4, 0.1) * fres + vec3(0.2, 0.4, 0.2) * (spec * 0.3);
    finalColor = vec4(finalRGB, 1.0);
}