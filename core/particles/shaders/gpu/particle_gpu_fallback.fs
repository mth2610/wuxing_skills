#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 u_vfxContrast;
uniform float u_emissiveBoost;
out vec4 finalColor;

#include "core/shaders/common/vfx_contrast.glsl"
#include "core/shaders/common/vfx_composite.glsl"

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 body = texel * fragColor;
    if (body.a < 0.01) discard;
    float emission = u_emissiveBoost;
    finalColor = VFX_ResolveEmission(body.rgb, emission, 1.0, body.a);
}
