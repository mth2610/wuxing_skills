#version 330

// ============================================================
// WUXING — prop_lit Vertex Shader (CORE_ISSUES.md Item 36)
//
// Lit material for map terrain/props, driven by raylib's own
// Material + DrawModel()/DrawModelEx() pipeline — NOT a skill VFX shader.
// Deliberately does not pull in core/shaders/common/vs_header.glsl: that
// header's u_time/viewPos/u_resolution uniforms are auto-bound by
// SkillManager_BeginShader(), which never runs for a plain DrawModel()
// call, and its VS_FinalOutput() doesn't know about tangents. Only the
// raylib-standard mvp/matModel (auto-bound by DrawMesh/DrawModel — see
// core/prop_lit.c for the shader.locs[] fixups this relies on) plus a
// tangent attribute for normal mapping are needed here.
//
// Procedural meshes MUST call GenMeshTangents(&mesh) before
// LoadModelFromMesh(), or vertexTangent will be all-zero and normal
// mapping will read as flat/zero. glTF-imported models usually ship
// tangents already — verify, don't assume.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;   // xyz = tangent, w = bitangent handedness sign

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec3 fragTangent;
out vec3 fragBitangent;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;

    mat3 normalMat = mat3(matModel);
    fragNormal  = normalize(normalMat * vertexNormal);
    fragTangent = normalize(normalMat * vertexTangent.xyz);
    // Re-orthogonalize against the normal (Gram-Schmidt) so a non-uniformly
    // scaled matModel can't skew the TBN basis.
    fragTangent = normalize(fragTangent - fragNormal * dot(fragNormal, fragTangent));
    fragBitangent = cross(fragNormal, fragTangent) * vertexTangent.w;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
