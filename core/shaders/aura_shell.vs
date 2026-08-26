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

    // NOT VS_FinalOutput. The only consumer draws this with DrawCoreSphere,
    // i.e. immediate mode, and BeginMode3D has already applied model-view on
    // the CPU — VS_FinalOutput would multiply matModel (= model x view for every
    // draw inside MyBeginMode3D, ENGINE_LANDMINES 9) a SECOND time and skew both
    // the position and the normal. glass_shell.vs carries the same note for the
    // same mesh; this file did not, and the fragment stage's fresnel was reading
    // a doubly-transformed normal against a world-space camera.
    // PASS BOTH ATTRIBUTES THROUGH UNTOUCHED. This material's only consumer
    // draws with DrawCoreSphere, i.e. immediate mode, and MyBeginMode3D's
    // rlPushMatrix() in RL_MODELVIEW arms rlgl's transformRequired — so
    // rlVertex3f AND rlNormal3f both transform on the CPU (rlgl.h:1529/1612)
    // and the attributes arrive ALREADY IN VIEW SPACE. matModel is then that
    // same view matrix, so VS_FinalOutput applies it a second time.
    //
    // MEASURED, not reasoned: third_party/vulkan/tests/rlvk_visual_test.c
    // scenario `imm_normal` sends a known normal down this exact path —
    //     raw vertexNormal      -> view * N        (delta 0.002)
    //     matModel*vertexNormal -> view * view * N (delta 0.005)
    // core/trails/shaders/trail_volume.vs carries the same finding at length.
    //
    // A first pass at this file kept matModel for the NORMAL on the reasoning
    // that rlgl transforms positions but not normals. That reasoning is wrong
    // and the test above says so; the frag stage's |N.V| was inverted for a day
    // because of it. In view space the camera is at the origin, so the view
    // vector is normalize(-fragPosition) and viewPos must not appear at all.
    vec3 dp = vertexPosition + disp;
    fragPosition = dp;
    fragNormal   = normalize(vertexNormal);
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(dp, 1.0);
}
