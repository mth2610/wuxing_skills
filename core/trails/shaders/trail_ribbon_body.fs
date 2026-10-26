#version 330 core
#include "core/shaders/common/vfx_composite.glsl"
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    if (fragColor.a <= 0.001) discard;
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 body = texel * fragColor;
    if (body.a < 0.003) discard;
    finalColor = VFX_ResolveBody(body.rgb, 1.0, body.a);
}
