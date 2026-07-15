// rlvk embedded default shader (fragment) — source for rlvk_shaders.h's rlvkDefaultFragSpv.
// Regenerate with scripts/gen_rlvk_shaders.sh after editing.
//
// Interface contract (must match rlvk.h):
//  - set 0, binding 0 = texture unit 0 (combined image sampler; bindings 0..15 are GL units)
//  - push_constant block byte-for-byte = rlvkPushConstants { mat4 mvp; vec4 colDiffuse; }
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 colDiffuse;
} pc;

layout(location = 0) out vec4 finalColor;

void main()
{
    finalColor = texture(texture0, fragTexCoord)*pc.colDiffuse*fragColor;
}
