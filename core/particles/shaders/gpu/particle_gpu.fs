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

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 lit = texel * fragColor;
    if (u_softFade > 0.0)
        lit.a *= SoftParticle_Factor(u_softFade);

    // Discard pixel trong suốt hoàn toàn để tối ưu fillrate
    if (lit.a < 0.01) discard;

    // GPU billboards have ONE blend law — both ParticleManager_Draw and
    // _DrawEmission wrap this in BLEND_ADDITIVE, and particle_manager.c calls
    // it an emissive-only material contract — so the resolver is fixed and
    // needs no permutation. mask stays 1.0: lit.a is the authored alpha, and
    // feeding it to both arguments would square it.
    finalColor = VFX_ResolveEmission(lit.rgb, 1.0, 1.0, lit.a);
}
