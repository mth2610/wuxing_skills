#version 330
#include "core/shaders/common/vs_header.glsl"

// ── FILAMENT TRAIL — vertex stage ───────────────────────────────────────────
//
// Does nothing but forward. Every bit of shape happened on the CPU:
// PMDroplet_BuildAlongPath swept the rings and PMDroplet_DrawEx emitted them.
//
// *** DELIBERATELY NOT VS_FinalOutput. ***
// This draws in IMMEDIATE MODE, and MyBeginMode3D's rlPushMatrix() in
// RL_MODELVIEW arms rlgl's transformRequired — so rlVertex3f AND rlNormal3f
// both transform on the CPU (rlgl.h:1529/1612) and the attributes arrive
// ALREADY IN VIEW SPACE. matModel is that same view matrix, so VS_FinalOutput
// would apply it a second time to both.
//
// MEASURED: third_party/vulkan/tests/rlvk_visual_test.c scenario `imm_normal`
//     raw vertexNormal      -> view * N          (delta 0.002)
//     matModel*vertexNormal -> view * view * N   (delta 0.005)
// core/trails/shaders/trail_volume.vs carries the full derivation, and
// core/docs/LANDMINES.md records a day lost to getting it half right.
//
// Consequence the fragment stage depends on: in view space the camera is at the
// ORIGIN, so the view vector is normalize(-fragPosition) and `viewPos` must
// never appear.

in vec4 vertexColor;

out vec4 vColor;

void main()
{
    fragPosition = vertexPosition;
    fragNormal   = normalize(vertexNormal);
    fragTexCoord = vertexTexCoord;
    vColor       = vertexColor;
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
