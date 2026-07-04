#version 330
#include "core/shaders/common/vs_header.glsl"

// Plasma morph: several slow traveling waves in different directions beat
// against each other, so the dome continuously changes silhouette — bulges
// wander across the surface instead of the whole ball pulsing in sync.
// Wavelengths stay long (>= ~30 world units) so the 36x48 sphere tessellation
// never aliases into facets (§11.2).
float getSwell(vec3 pos) {
    // Spatial frequencies ×100 (world-space: pos now in metres, was cm).
    // Amplitude ÷100: 3.4 cm → 0.034 m.
    float travel = sin(pos.x * 7.5 + pos.z * 5.5 + u_time * 1.1);
    float roll   = sin(pos.y * 6.5 - u_time * 0.9);
    float cross1 = cos(pos.x * 5.0 - u_time * 0.7) * sin(pos.z * 6.0 + u_time * 1.4);
    float breathe = sin(u_time * 0.8) * 0.3;

    return (travel * 0.45 + roll * 0.35 + cross1 * 0.5 + breathe) * 0.034;
}

void main() {
    vec3 displaced = vertexPosition + vertexNormal * getSwell(vertexPosition);
    VS_FinalOutput(displaced);
}
