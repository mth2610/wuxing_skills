#version 330
// GPU-instancing variant of effect_material.vs — used for N copies of one
// EffectMaterial-backed shape drawn as a single DrawMeshInstanced call
// (e.g. VFX_ComposeFloatingStones' orbiting rocks, core/composition/vc_earth.inl).
// See CORE_ISSUES.md Item 40 and crystal_instanced.vs for the reference
// implementation this mirrors.
//
// Does NOT #include vs_header.glsl: the `instanceTransform` attribute
// (per-instance, auto-bound by raylib on DrawMeshInstanced) replaces the
// single per-draw-call `matModel` uniform vs_header.glsl assumes — mixing
// both in the shared header risks breaking every other shader that includes
// it, and reading an unbound `in mat4 instanceTransform` when not drawn via
// DrawMeshInstanced is undefined behavior across GPU drivers.
//
// effect_material.fs is reused completely unchanged.

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in mat4 instanceTransform; // raylib DrawMeshInstanced auto-binds, 1 matrix/instance

uniform mat4 mvp;      // = matProjection * matView when instancing (raylib doesn't premultiply transform)
uniform mat4 matModel; // identity by default (SkillManager_BeginShader) — multiplied by instanceTransform below
uniform float u_time;  // auto-bound by SkillManager_BeginShader
uniform float u_distortionStrength; // 0..1

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    // Same wobble formula as effect_material.vs, but phase driven by
    // vertexNormal (local-space, always small) since vertexPosition here is
    // also local-space (pre-instanceTransform) — matches the non-instanced
    // shader's rationale for avoiding large world-coordinate precision loss.
    float wobble = sin(vertexNormal.x * 6.0 + u_time * 4.0)
                 * cos(vertexNormal.z * 6.0 - u_time * 3.0);
    vec3 pos = vertexPosition + vertexNormal * wobble * u_distortionStrength;

    mat4 instanceModel = matModel * instanceTransform;
    fragPosition = vec3(instanceModel * vec4(pos, 1.0));
    fragNormal = normalize(vec3(instanceModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;

    gl_Position = mvp * instanceTransform * vec4(pos, 1.0);
}
