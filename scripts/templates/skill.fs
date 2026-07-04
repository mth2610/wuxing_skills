#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 baseColor = fragColor.rgb;
    finalColor = vec4(baseColor * (0.3 + 0.7 * diff), fragColor.a);
}
