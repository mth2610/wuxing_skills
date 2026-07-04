#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D causticsTex; // caustics
uniform sampler2D flowTex;     // flowmap
uniform float u_time;
out vec4 finalColor;

#include "core/shaders/common/fx.glsl"

void main() {
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    if (dist > 0.5) discard;
    
    // Flow map blending
    vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
    float caust1 = flowBlend(causticsTex, fragTexCoord * 3.0, flowDir, 0.5, 0.15, u_time);
    float caust2 = flowBlend(causticsTex, fragTexCoord * 5.0 + 0.3, flowDir, -0.4, 0.1, u_time);
    float caustic = caust1 * 0.7 + caust2 * 0.5;
    
    // Smooth fade out at edges
    float edgeMask = smoothstep(0.5, 0.4, dist);
    
    // Cyan base color + bright caustics
    vec3 baseColor = vec3(0.0, 0.6, 1.0) * 0.3; // base blue
    baseColor += vec3(0.5, 0.9, 1.0) * caustic * 1.5;
    
    finalColor = vec4(baseColor, fragColor.a * edgeMask);
}
