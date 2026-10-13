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
// The fragment shader derives ALL texture and noise coordinates from
// fragTexCoord (baked to world meters at mesh creation by TilePlaneUVs;
// see the Item 38 follow-up in CORE_ISSUES.md), NOT from fragPosition.xz.
// fragTexCoord is therefore the critical varying here and must stay a
// plain pass-through of vertexTexCoord. fragPosition is still needed for
// the VFX light accumulation, fragNormal for the sun/VFX lighting. No
// tangent attribute — grassDetail is a plain grayscale grain multiplier,
// not a tangent-space normal map.
//
// The vertex work is already minimal (one mat4*vec4 for world position,
// one mat3 for the normal, one mvp). The ground is a handful of large
// quads, so there is no per-vertex cost worth shaving; the optimization
// effort for this material lives in grass_material.fs.
//
// Note: mat3(matModel) is only a correct normal transform for rotation +
// uniform scale. If the ground model is ever drawn with non-uniform
// scale, switch to the inverse-transpose (raylib's matNormal uniform).
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
