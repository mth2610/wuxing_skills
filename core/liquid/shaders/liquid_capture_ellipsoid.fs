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
    if(u_backDepth!=0 && u_matchFrontMaterial>0.5) {
        vec4 front=texelFetch(u_frontDepthTex,ivec2(gl_FragCoord.xy),0);
        if(front.r>=0.99999 || abs(front.b-v_material)>0.25) discard;
    }
    vec4 nearClip = u_inverseProjection * vec4(v_ndc, -1.0, 1.0);
    vec4 farClip = u_inverseProjection * vec4(v_ndc, 1.0, 1.0);
    vec3 nearView = nearClip.xyz / nearClip.w;
    vec3 farView = farClip.xyz / farClip.w;
    vec3 rayOriginView = u_orthographic != 0 ? nearView : vec3(0.0);
    vec3 rayDirectionView = normalize(u_orthographic != 0 ? farView-nearView : nearView);

    /* Radii are aligned with the WORLD axes, not the camera axes. Rotate the
     * ray relative to the centre into that frame before scaling to a unit ball.
     * Keeping the origin relative avoids cancellation of large world positions. */
    mat3 viewToWorld = mat3(u_viewToWorld);
    vec3 o = (viewToWorld * (rayOriginView-v_centerView)) / v_radii;
    vec3 d = (viewToWorld * rayDirectionView) / v_radii;
    float a = dot(d,d);
    float closest = -dot(o,d)/a;
    vec3 residual = o+d*closest;
    /* B*B-A*C loses a centimetre kernel's entire chord at gameplay distance.
     * Closest approach keeps the discriminant on the unit ellipsoid's scale. */
    float chordMetric = 1.0-dot(residual,residual);
    float chordWidth = max(fwidth(chordMetric),0.000001);
    if (chordMetric < 0.0) discard;
    float halfChord = sqrt(max(chordMetric,0.0)/a);
    float root = closest + (u_backDepth != 0 ? halfChord : -halfChord);
    if (root < 0.0) discard;
    vec3 surfaceView = rayOriginView+rayDirectionView*root;
    vec4 clip = u_projection*vec4(surfaceView,1.0);
    if (clip.w <= 0.0) discard;
    float ndcDepth = clip.z/clip.w;
    if (ndcDepth < -1.0 || ndcDepth > 1.0) discard;
    float depth = ndcDepth*0.5+0.5;
    /* The back capture has an ordinary LESS depth test: complementary depth
     * makes the farthest real root win without optional float MAX blending. */
    gl_FragDepth = u_backDepth != 0 ? 1.0-depth : depth;
    float coverage = smoothstep(0.0,chordWidth,chordMetric);
    finalColor = u_backDepth != 0 ? vec4(depth,1.0,0.0,1.0)
                                 : vec4(depth,coverage,v_material,1.0);
}
