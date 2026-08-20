#version 330

// ============================================================
// WUXING — grass_material Vertex Shader (CORE_ISSUES.md Item 38,
// supersedes Item 37)
//
// Texture-blend hybrid ground material. Drawn via raylib's Material +
// DrawModel()/DrawModelEx() pipeline (like prop_lit, CORE_ISSUES.md
// Item 36) — NOT a skill VFX shader, so it deliberately does not pull in
// core/shaders/common/vs_header.glsl (that header's uniforms are only
// auto-bound by SkillManager_BeginShader, which never runs for a plain
// DrawModel() call).
//
// No change needed from Item 37: the fragment shader derives all of
// its texture UVs from world-space fragPosition.xz (not fragTexCoord),
// same "no tiling-seam-decision, works at any mesh size" reasoning as
// Item 37's noise sampling — so this vertex shader just needs to keep
// passing fragPosition/fragNormal through, which it already did.
// fragTexCoord is passed through unused for parity with prop_lit's
// vertex layout; no tangent attribute here — grassDetail is a plain
// grayscale grain multiplier, not a tangent-space normal map.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;

    mat3 normalMat = mat3(matModel);
    fragNormal = normalize(normalMat * vertexNormal);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
