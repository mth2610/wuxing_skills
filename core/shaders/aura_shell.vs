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
    // POSITION AND NORMAL NEED DIFFERENT TREATMENT HERE, and that is the whole
    // trap. rlgl immediate mode transforms VERTICES on the CPU but passes
    // NORMALS through raw, so DrawCoreSphere hands this stage a view-space
    // position next to a world-space normal. Proved, not assumed: rendering
    // fract(length(fragPosition)) gives a gradient CONCENTRIC with the
    // silhouette, which is only true if the minimum is the fragment nearest the
    // camera (ENGINE_LANDMINES 9's prescribed probe).
    //
    //   position: already view space -> pass through, do NOT re-apply matModel
    //   normal:   still world space  -> matModel (= model x view) puts it in
    //             view space, which is what VS_FinalOutput was doing correctly
    //
    // The original code applied matModel to BOTH, so the position was
    // double-transformed; a first attempt at this fix removed it from both, so
    // the normal was left in world space and dotted against a view-space view
    // vector. Either way the fresnel is meaningless, and both look like a
    // plausible gradient rather than like a bug.
    vec3 dp = vertexPosition + disp;
    fragPosition = dp;
    fragNormal   = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(dp, 1.0);
}
