#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/triplanar.glsl"

// Test: Item 4a triplanar mapping (CORE_ISSUES.md) — ProceduralMesh_DrawRock
// has no UV (rlBegin immediate-mode, position+normal only), so this exercises
// triplanarWeights/triplanarNoise on the jagged rock facets, confirming no
// stretching/streaking across faces with extreme normal direction changes.

uniform vec4 u_colorA;
uniform vec4 u_colorB;
uniform float u_scale;
uniform float u_sharpness;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 weights = triplanarWeights(normal, u_sharpness);
    float pattern = triplanarNoise(fragPosition, weights, u_scale);

    vec3 baseColor = mix(u_colorA.rgb, u_colorB.rgb, pattern);

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
    float diff = calcDiffuse(normal, lightDir, 0.2);
    float spec = calcSpecular(normal, lightDir, viewDir, 48.0);
    float fresnel = calcFresnel(normal, viewDir, 3.0);

    vec3 color = baseColor * diff + vec3(spec) * 0.3 + u_colorB.rgb * fresnel * 0.25;
    finalColor = vec4(color, u_colorA.a);
}
