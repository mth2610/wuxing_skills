#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/lighting.glsl"

// Test: dissolve edge glow (fx.glsl's dissolveCalc + edgeFactor — already
// existed, this just exercises/confirms it).

uniform vec4 u_color;
uniform vec3 u_edgeColor;
uniform float u_dissolve;

void main() {
    float n = hash3(floor(fragPosition * 6.0));
    float edgeFactor;
    if (dissolveCalc(n, u_dissolve, 0.1, edgeFactor) >= 1.0) discard;

    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
    float diff = calcDiffuse(fragNormal, lightDir, 0.4);

    vec3 baseColor = u_color.rgb * diff;
    vec3 color = mix(baseColor, u_edgeColor * 2.0, edgeFactor);
    finalColor = vec4(color, u_color.a);
}
