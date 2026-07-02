#version 330
#include "core/shaders/common/vs_header.glsl"

// Plasma morph: several slow traveling waves in different directions beat
// against each other, so the dome continuously changes silhouette — bulges
// wander across the surface instead of the whole ball pulsing in sync.
// Wavelengths stay long (>= ~30 world units) so the 36x48 sphere tessellation
// never aliases into facets (§11.2).
float getSwell(vec3 pos) {
    // traveling bulge sweeping around the equator
    float travel = sin(pos.x * 0.075 + pos.z * 0.055 + u_time * 1.1);
    // vertical roll climbing the dome
    float roll   = sin(pos.y * 0.065 - u_time * 0.9);
    // counter-phase cross wave — breaks the symmetry of the first two
    float cross1 = cos(pos.x * 0.05 - u_time * 0.7) * sin(pos.z * 0.06 + u_time * 1.4);
    // slow whole-body breathing, kept small
    float breathe = sin(u_time * 0.8) * 0.3;

    return (travel * 0.45 + roll * 0.35 + cross1 * 0.5 + breathe) * 3.4;
}

void main() {
    vec3 displaced = vertexPosition + vertexNormal * getSwell(vertexPosition);
    VS_FinalOutput(displaced);
}
