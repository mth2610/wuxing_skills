#version 330 core
// REQUIRE_ES31
// Fragment shader — dùng chung cho cả COMPUTE path và CPU/VBO path

#ifdef GL_ES
precision highp float;
#endif

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

    // Cắt bỏ ngay các góc quad trong suốt TRƯỚC khi lấy mẫu texture độ sâu (SoftParticle_Factor)
    if (lit.a < 0.01) discard;

    if (u_softFade > 0.0) {
        lit.a *= SoftParticle_Factor(u_softFade);
        if (lit.a < 0.004) discard;
    }

    // Separate alpha, additive, and premultiplied draws share resident storage.
    // lit.a already contains coverage; do not multiply coverage twice.
    if (u_blendLaw == 0) {
        finalColor = VFX_ResolveBody(lit.rgb, 1.0, lit.a);
    } else if (u_blendLaw == 2) {
        float corePin = smoothstep(0.72, 0.98, texel.a);
        vec3 carrier = lit.rgb * 0.55;
        vec3 coreRgb = mix(lit.rgb, mix(lit.rgb, vec3(1.0, 0.97, 0.90), 0.65) * 1.45, corePin * 0.65);
        finalColor = VFX_ResolvePremultiplied(carrier, 1.0, clamp(lit.a * 0.52, 0.0, 0.78),
                                              coreRgb * lit.a, 1.0, 1.0);
    } else {
        finalColor = VFX_ResolveEmission(lit.rgb, 1.0, 1.0, lit.a);
    }
}
