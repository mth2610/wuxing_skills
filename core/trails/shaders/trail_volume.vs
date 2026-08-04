#version 330
#include "core/shaders/common/vs_header.glsl"

// ── TRAIL VOLUME — vertex stage for the swept-tube volume shading ───────────
//
// It does no displacement. Every bit of shape already happened on the CPU:
// core/deform churns the section and core/geometry/pm_tube.inl builds the
// rings, so by the time a vertex arrives here its position is final. This
// stage exists only to hand the fragment stage the three things it cannot
// reconstruct — the interpolated normal, the position, and the per-vertex
// tint.
//
// VS_FinalOutput writes fragPosition and fragNormal through `matModel`, which
// inside a 3D pass is model x VIEW (ENGINE_LANDMINES §9). Both therefore land
// in VIEW space, and the fragment stage is written for view space throughout.
// That is not a workaround, it is the cheap frame: the camera sits at the
// origin there, so the view vector needs no uniform at all.

in vec4 vertexColor;

out vec4 vColor;

void main() {
    // The tube's fade mask rides in the alpha (pm_tube.inl PMTube_DrawFaded):
    // a per-vertex attribute, not a uniform, so nothing has to be re-pushed
    // between draws.
    vColor = vertexColor;
    VS_FinalOutput(vertexPosition);
}
