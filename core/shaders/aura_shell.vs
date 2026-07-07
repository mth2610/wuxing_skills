#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Aura Shell — Vertex Shader
// Backing VS for AuraShellMaterial (material_system.h).
// Designed for open cylinder meshes (no caps). The XZ normal
// is radial so angular ripple is applied in that plane; Y
// displacement adds a flowing edge-wave at the top and bottom.
// ============================================================

uniform float u_displaceAmp;

void main() {
    vec3 n = vertexNormal;
    // Radial angle phase — n.x/n.z are cos/sin of the ring angle.
    // Three incommensurate angular+height frequencies avoid visible tiling.
    float h  = vertexPosition.y;
    float w1 = sin(n.x * 4.0 - n.z * 2.0 + u_time * 1.3)
             * sin(h   * 3.0 + u_time * 0.7);
    float w2 = sin(n.z * 5.0 + n.x * 3.0 - u_time * 1.8)
             * sin(h   * 6.0 - u_time * 1.1);
    float w3 = sin(h   * 4.5 - n.x * 6.0 + n.z * 4.0 + u_time * 2.3);
    float wobble = w1 * 0.5 + w2 * 0.3 + w3 * 0.2;

    // Radial push (XZ only — keeps the cylinder shape intact) plus a small Y
    // ripple that makes the top and bottom edges undulate like a live membrane.
    vec3 disp;
    disp.x = n.x * wobble * u_displaceAmp;
    disp.y = sin(h * 8.0 - n.x * 3.0 + n.z * 2.5 + u_time * 2.1)
           * u_displaceAmp * 0.25;
    disp.z = n.z * wobble * u_displaceAmp;

    VS_FinalOutput(vertexPosition + disp);
}
