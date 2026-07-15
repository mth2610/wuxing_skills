// rlvk embedded default shader (vertex) — source for rlvk_shaders.h's rlvkDefaultVertSpv.
// Regenerate with scripts/gen_rlvk_shaders.sh after editing.
//
// Interface contract (must match rlvk.h):
//  - attribute locations 0/1/3 = position/texcoord/color (rlvkInitDefaultShader's attribLocs)
//  - push_constant block byte-for-byte = rlvkPushConstants { mat4 mvp; vec4 colDiffuse; }
//  - Vulkan 1.1 core baseline: GL-style [-1,1] clip-z is remapped to [0,1] HERE, in the
//    shader epilogue — VK_EXT_depth_clip_control is NOT used (absent on most 1.1 devices).
//    Runtime-compiled user shaders get the same epilogue injected by the shaderc path.
#version 450

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 3) in vec4 vertexColor;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 colDiffuse;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    gl_Position  = pc.mvp*vec4(vertexPosition, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w)*0.5;   // GL [-1,1] -> VK [0,1] clip-z
}
