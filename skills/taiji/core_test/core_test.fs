#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

uniform vec4 u_color;

void main() {
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float diff  = calcDiffuse(fragNormal, lightDir, 0.35);
    float spec  = calcSpecular(fragNormal, lightDir, viewDir, 24.0);
    float fres  = calcFresnel(fragNormal, viewDir, 2.5);

    vec3 color = u_color.rgb * diff + vec3(spec) + u_color.rgb * fres * 0.6;
    finalColor = vec4(color, u_color.a);
}
