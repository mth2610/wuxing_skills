#version 330
out vec4 finalColor;
uniform sampler2D u_frontDepthTex;
uniform float u_matchFrontMaterial;
uniform float u_materialId;

/* Back-depth twin of liquid_capture.fs for the real-geometry path (DrawSphereEx).
 * Real spheres DO have back faces, so the far surface comes from front-face
 * culling at the call site rather than a second analytic root — but the
 * reduction still has to be a MAX, hence the same complement-depth trick as
 * liquid_capture_particle_back.fs. */
void main() {
    if(u_matchFrontMaterial>0.5) {
        vec4 front=texelFetch(u_frontDepthTex,ivec2(gl_FragCoord.xy),0);
        if(front.r>=0.99999 || abs(front.b-u_materialId)>0.25) discard;
    }
    gl_FragDepth = 1.0 - gl_FragCoord.z;
    finalColor = vec4(gl_FragCoord.z, 1.0, 0.0, 1.0);
}
