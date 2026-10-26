#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 v_ndc;
flat in vec3 v_centerView;
flat in vec3 v_radii;
flat in float v_material;
out vec4 finalColor;
uniform sampler2D u_frontDepthTex;
uniform float u_matchFrontMaterial;
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform int u_orthographic;
uniform int u_backDepth;

void main() {
    vec4 front = vec4(0.0);
    bool checkFront = (u_backDepth != 0 && u_matchFrontMaterial > 0.5);
    if (checkFront) {
        front = texelFetch(u_frontDepthTex, ivec2(gl_FragCoord.xy), 0);
        if (front.r >= 0.99999 || abs(front.b - v_material) > 0.25) discard;
    }

    vec4 nearClip = u_inverseProjection * vec4(v_ndc, -1.0, 1.0);
    vec3 nearView = nearClip.xyz / max(abs(nearClip.w), 1e-6) * sign(nearClip.w);
    vec3 rayOriginView;
    vec3 rayDirectionView;

    /* Avoid computing farClip matrix multiply and perspective divide in perspective mode */
    if (u_orthographic != 0) {
        vec4 farClip = u_inverseProjection * vec4(v_ndc, 1.0, 1.0);
        vec3 farView = farClip.xyz / max(abs(farClip.w), 1e-6) * sign(farClip.w);
        rayOriginView = nearView;
        rayDirectionView = normalize(farView - nearView);
    } else {
        rayOriginView = vec3(0.0);
        rayDirectionView = normalize(nearView);
    }

    /* Radii are aligned with the WORLD axes, not the camera axes. Rotate the
     * ray relative to the centre into that frame before scaling to a unit ball.
     * Keeping the origin relative avoids cancellation of large world positions. */
    mat3 viewToWorld = mat3(u_viewToWorld);
    vec3 invRadii = 1.0 / max(v_radii, vec3(1e-6));
    vec3 o = (viewToWorld * (rayOriginView - v_centerView)) * invRadii;
    vec3 d = (viewToWorld * rayDirectionView) * invRadii;
    float a = max(dot(d, d), 1e-12);
    float closest = -dot(o, d) / a;
    vec3 residual = o + d * closest;
    /* B*B-A*C loses a centimetre kernel's entire chord at gameplay distance.
     * Closest approach keeps the discriminant on the unit ellipsoid's scale. */
    float chordMetric = 1.0 - dot(residual, residual);
    float chordWidth = (u_backDepth == 0) ? max(fwidth(chordMetric), 1e-6) : 1e-6;
    if (chordMetric < 0.0) discard;

    float halfChord = sqrt(max(chordMetric, 0.0) / a);
    float root = closest + (u_backDepth != 0 ? halfChord : -halfChord);
    if (root < 0.0) discard;

    vec3 surfaceView = rayOriginView + rayDirectionView * root;
    float clipZ = u_projection[0][2] * surfaceView.x + u_projection[1][2] * surfaceView.y +
                  u_projection[2][2] * surfaceView.z + u_projection[3][2];
    float clipW = u_projection[0][3] * surfaceView.x + u_projection[1][3] * surfaceView.y +
                  u_projection[2][3] * surfaceView.z + u_projection[3][3];
    if (clipW <= 0.0) discard;
    float ndcDepth = clipZ / clipW;
    if (ndcDepth < -1.0 || ndcDepth > 1.0) discard;
    float depth = ndcDepth * 0.5 + 0.5;
    if (checkFront && depth < front.r - 1e-5) discard;

    /* The back capture has an ordinary LESS depth test: complementary depth
     * makes the farthest real root win without optional float MAX blending. */
    gl_FragDepth = u_backDepth != 0 ? 1.0 - depth : depth;
    float coverage = (u_backDepth != 0) ? 1.0 : smoothstep(0.0, chordWidth, chordMetric);
    finalColor = u_backDepth != 0 ? vec4(depth, 1.0, 0.0, 1.0)
                                  : vec4(depth, coverage, v_material, 1.0);
}
