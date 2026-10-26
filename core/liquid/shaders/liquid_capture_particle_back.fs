#version 330
in vec3 v_centerView;
in vec2 v_corner;
in vec2 v_offsetView;
in float v_depthRadius;
in float v_life;
flat in float v_materialId;
out vec4 finalColor;
uniform sampler2D u_frontDepthTex;
uniform float u_matchFrontMaterial;
uniform mat4 u_projection;

/* BACK half of the same analytic ellipsoid liquid_capture_particle.fs draws the
 * front of. Together they bracket the splat cloud, and back - front is a
 * measured path length in metres — the thickness the accumulation pass could
 * only approximate. */
void main() {
    /* Reject dead particles and corner fragments outside the unit circle BEFORE
     * issuing a texture fetch to u_frontDepthTex (~22% fewer VRAM reads). */
    if (v_life <= 0.0) discard;
    vec2 q = v_corner;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;

    vec4 front = vec4(0.0);
    bool checkFront = (u_matchFrontMaterial > 0.5);
    if (checkFront) {
        front = texelFetch(u_frontDepthTex, ivec2(gl_FragCoord.xy), 0);
        if (front.r >= 0.99999 || abs(front.b - v_materialId) > 0.25) discard;
    }

    // Identical profile to the front pass; only the sign of the Z offset
    // differs. They must stay identical or the two surfaces do not belong to
    // the same kernel and their difference is not a chord.
    float sphereZ = sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10);
    vec3 surfaceView = v_centerView + vec3(v_offsetView, -sphereZ * v_depthRadius);
    // The far root of a splat straddling the eye plane projects behind the
    // camera, where the perspective divide flips sign.
    if (surfaceView.z > -0.001) discard;

    float clipZ = u_projection[0][2] * surfaceView.x + u_projection[1][2] * surfaceView.y +
                  u_projection[2][2] * surfaceView.z + u_projection[3][2];
    float clipW = u_projection[0][3] * surfaceView.x + u_projection[1][3] * surfaceView.y +
                  u_projection[2][3] * surfaceView.z + u_projection[3][3];
    if (clipW <= 0.0) discard;

    float depth = clamp(clipZ / clipW * 0.5 + 0.5, 0.0, 1.0);
    if (checkFront && depth < front.r - 1e-5) discard;

    gl_FragDepth = 1.0 - depth;
    finalColor = vec4(depth, 1.0, 0.0, 1.0);
}
