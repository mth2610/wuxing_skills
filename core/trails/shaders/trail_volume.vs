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
// *** THIS STAGE DELIBERATELY DOES NOT CALL VS_FinalOutput. ***
// Root-caused 06/08/2026 and MEASURED, not reasoned:
// third_party/vulkan/tests/rlvk_visual_test.c scenario `imm_normal` sends a
// known normal down this exact draw path and reads it back numerically.
//
//     Nworld = (0.30, 0.90, -0.32)
//     raw vertexNormal      -> (0.43, 0.85, 0.30) = view * N        (delta 0.002)
//     matModel*vertexNormal -> (0.18, 0.56, 0.80) = view * view * N (delta 0.005)
//
// Why: PMTube_DrawFaded draws in IMMEDIATE MODE, and main.c's MyBeginMode3D
// calls rlPushMatrix() in RL_MODELVIEW, which arms rlgl's `transformRequired`
// and parks the VIEW matrix in State.transform. From there rlVertex3f and
// rlNormal3f transform every vertex ON THE CPU (rlgl.h:1529/1612), so the
// attributes reaching this shader are ALREADY IN VIEW SPACE — and the batch
// flush then uploads matModel = State.transform = that same view matrix
// (rlgl.h:3082, mirrored in rlvk_core.inl:595). VS_FinalOutput's
// `matModel * vertexNormal` therefore applies the view rotation A SECOND TIME.
//
// That double transform is the whole of the |N.V| inversion documented in
// core/docs/VOLUME_SHADING_HANDOFF.md. It also explains why the old debug
// mode 14 (attribute normal vs dFdx of fragPosition) came back WHITE: both
// operands were doubly transformed by the same matrix, so they agreed with
// each other while being jointly wrong.
//
// So: pass the attributes through untouched. They are view space, the frag
// stage is written for view space, and in view space the camera is at the
// origin — the view vector needs no uniform at all.
//
// NOTE FOR ANYONE FIXING THIS AT THE ROOT: if MyBeginMode3D ever drops its
// RL_MODELVIEW rlPushMatrix() (raylib's own BeginMode3D does not have it, and
// removing it would put fragPosition back in WORLD space engine-wide and
// retire ENGINE_LANDMINES §9), this file must go back to VS_FinalOutput and
// the frag stage back to `viewPos - fragPosition`. The `imm_normal` scenario
// is the tripwire: it FAILS with "matModel is identity here" in that world.

in vec4 vertexColor;

out vec4 vColor;

void main() {
    // The tube's fade mask rides in the alpha (pm_tube.inl PMTube_DrawFaded):
    // a per-vertex attribute, not a uniform, so nothing has to be re-pushed
    // between draws.
    vColor = vertexColor;

    fragPosition = vertexPosition;             // already VIEW space (see above)
    fragNormal   = normalize(vertexNormal);    // already VIEW space (see above)
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
