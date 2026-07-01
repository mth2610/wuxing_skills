#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Effect Material — Generic Parametrized Material (Vertex Shader)
// Backing shader for Material_LoadCustom() (core/skill_helper.h,
// CORE_API.md Shader Material System). u_distortionStrength drives a
// small procedural wobble so "distortion" is visible without every
// skill needing its own VS — see effect_material.fs for the rest of
// the parametrized look (rim/fresnel/emissive/dissolve/texture1).
// ============================================================

uniform float u_distortionStrength; // 0..1

void main() {
    // Phase driven by vertexNormal (always small, [-1..1]), NOT vertexPosition —
    // vertexPosition is a raw world coordinate here (immediate-mode draw calls
    // bake world position directly, matModel is identity per Rule D), so an
    // arena-scale value like ~600 fed into sin()/cos() loses precision on GPU
    // and produces noisy, unstable wobble instead of a smooth one.
    float wobble = sin(vertexNormal.x * 6.0 + u_time * 4.0)
                 * cos(vertexNormal.z * 6.0 - u_time * 3.0);
    vec3 displacedPos = vertexPosition + vertexNormal * wobble * u_distortionStrength;
    VS_FinalOutput(displacedPos);
}
