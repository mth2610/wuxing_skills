// ============================================================
// WUXING — Common Vertex Shader Header, GPU-INSTANCING variant
//
// Interface-identical twin of vs_header.glsl: same attributes, same varyings,
// same VS_FinalOutput(vec3) seam. Only the transform differs — the per-instance
// `instanceTransform` attribute (raylib auto-binds one matrix per instance on
// DrawMeshInstanced) replaces the single per-draw-call matModel.
//
// Selected by the INSTANCED permutation, never included directly: a .vs opens
// with an ifdef/else pair that pulls in this file when INSTANCED is defined and
// vs_header.glsl otherwise. See core/shaders/crystal.vs for the exact form.
//
// Do NOT paste that form here as an example. shader_preprocessor.c is a purely
// textual expander with no notion of comments, so an include directive written
// inside a // comment is still expanded — and in THIS file that means the file
// includes itself, eight levels deep, until the depth limit stops it. Worse,
// the parser takes the next double-quote ANYWHERE after the directive as the
// path, so a commented directive with no quotes on its line silently picks up
// a quote from further down the file.
//
// WHY a compile-time #ifdef and not a runtime `if`: reading an unbound
// `in mat4 instanceTransform` when the draw is NOT DrawMeshInstanced is
// undefined behaviour across GPU drivers. The #ifdef removes the declaration
// itself from the non-instanced program; a runtime branch would not.
//
// TRADE-OFF, unchanged from the hand-copied variants this replaces: uniforms
// are per-draw-call, so anything like u_growProgress is SHARED by the whole
// batch — no staggered per-instance animation. Per-instance position/rotation/
// scale still vary, through instanceTransform.
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in mat4 instanceTransform; // raylib DrawMeshInstanced auto-binds, 1 matrix/instance

uniform mat4  mvp;      // = matProjection * matView when instancing (raylib does NOT premultiply transform)
uniform mat4  matModel; // identity by default (SkillManager_BeginShader) — multiplied by instanceTransform below
uniform float u_time;        // auto-bound bởi SkillManager_BeginShader
uniform vec3  viewPos;       // auto-bound bởi SkillManager_BeginShader
uniform vec2  u_resolution;  // auto-bound bởi SkillManager_BeginShader

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void VS_FinalOutput(vec3 displacedPos) {
    mat4 instanceModel = matModel * instanceTransform;
    fragPosition = vec3(instanceModel * vec4(displacedPos, 1.0));
    fragNormal   = normalize(vec3(instanceModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * instanceTransform * vec4(displacedPos, 1.0);
}
