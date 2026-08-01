#version 330 core
// REQUIRE_ES31
// Fragment shader — dùng chung cho cả COMPUTE path và CPU/VBO path

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 u_resolution;

#include "core/shaders/common/soft_particle.glsl"

uniform float u_softFade;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    finalColor  = texel * fragColor;
    if (u_softFade > 0.0)
        finalColor.a *= SoftParticle_Factor(u_softFade);

    // Discard pixel trong suốt hoàn toàn để tối ưu fillrate
    if (finalColor.a < 0.01) discard;
}
