#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Plasma Shell — Vertex Shader
// Backing VS for PlasmaMaterial (core/material/material_system.h).
// Undulates the sphere surface with layered sines so the orb's
// silhouette is a living lumpy membrane, never a perfect ball.
//
// Phase domain is vertexNormal (unit-length), NOT vertexPosition —
// immediate-mode draws bake raw world coords into vertexPosition
// (matModel = identity per Android Rule D), and arena-scale values
// fed into sin() lose precision on GPU (see effect_material.vs).
// ============================================================

uniform float u_displaceAmp; // world-units displacement amplitude

void main() {
    vec3 n = vertexNormal;
    // Three incommensurate wave layers -> irregular, non-repeating lumps.
    float w1 = sin(n.x * 5.0 + u_time * 1.1) * sin(n.y * 4.0 - u_time * 0.7);
    float w2 = sin(n.y * 7.0 - u_time * 1.7) * sin(n.z * 6.0 + u_time * 1.3);
    float w3 = sin(n.z * 3.0 + u_time * 0.9) * sin(n.x * 8.0 - u_time * 1.9);
    float wobble = w1 * 0.5 + w2 * 0.3 + w3 * 0.2;
    vec3 displacedPos = vertexPosition + vertexNormal * wobble * u_displaceAmp;
    VS_FinalOutput(displacedPos);
}
