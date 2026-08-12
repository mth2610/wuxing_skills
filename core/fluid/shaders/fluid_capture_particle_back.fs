#version 330
in vec3 v_centerView;
in vec2 v_corner;
in vec2 v_offsetView;
in float v_depthRadius;
in float v_life;
out vec4 finalColor;
uniform mat4 u_projection;

/* BACK half of the same analytic ellipsoid fluid_capture_particle.fs draws the
 * front of. Together they bracket the splat cloud, and back - front is a
 * measured path length in metres — the thickness the accumulation pass could
 * only approximate. Because both roots come from the same stretched kernel,
 * anisotropy is carried into thickness automatically; the deleted accumulation
 * pass would have needed its chord length stretched by hand to match.
 *
 * Point sprites have no back faces to cull, so "the far surface" is the far
 * root of the same equation, and the reduction has to be a MAX instead of the
 * depth buffer's MIN. Writing the COMPLEMENT of the depth does exactly that
 * with an ordinary depth test: the fragment that survives 'smallest 1-depth' is
 * the one with the largest depth. The alternatives both have portability costs
 * this does not — a MAX blend equation is optional on R32F (rlvk detects that
 * as Caps.floatBlendR32), and rlgl exposes no depth-func setter at all. */
void main() {
    if (v_life <= 0.0) discard;
    vec2 q = v_corner;
    float r2 = dot(q, q);
    if (r2 >= 1.0) discard;
    // Identical profile to the front pass; only the sign of the Z offset
    // differs. They must stay identical or the two surfaces do not belong to
    // the same kernel and their difference is not a chord.
    float sphereZ = sqrt(max(0.0, 1.0 - r2 * 0.90)) * (1.0 - r2 * 0.10);
    vec3 surfaceView = v_centerView + vec3(v_offsetView, -sphereZ * v_depthRadius);
    // The far root of a splat straddling the eye plane projects behind the
    // camera, where the perspective divide flips sign.
    if (surfaceView.z > -0.001) discard;
    vec4 clip = u_projection * vec4(surfaceView, 1.0);
    float depth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);
    gl_FragDepth = 1.0 - depth;
    finalColor = vec4(depth, 1.0, 0.0, 1.0);
}
