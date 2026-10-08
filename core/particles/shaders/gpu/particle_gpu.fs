#version 330 core
// REQUIRE_ES31
// Fragment shader — dùng chung cho cả COMPUTE path và CPU/VBO path

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 u_resolution;

#include "core/shaders/common/soft_particle.glsl"
#include "core/shaders/common/vfx_composite.glsl"

uniform float u_softFade;
uniform int u_blendLaw;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 lit = texel * fragColor;
    if (u_softFade > 0.0)
        lit.a *= SoftParticle_Factor(u_softFade);

    // Discard pixel trong suốt hoàn toàn để tối ưu fillrate
    if (lit.a < 0.01) discard;

    // Separate alpha and additive draws share resident particle storage.
    // lit.a already contains coverage; do not multiply coverage twice.
    finalColor = u_blendLaw==0 ? VFX_ResolveBody(lit.rgb,1.0,lit.a)
        : VFX_ResolveEmission(lit.rgb, 1.0, 1.0, lit.a);
}
