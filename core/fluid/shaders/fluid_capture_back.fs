#version 330
out vec4 finalColor;

/* Back-depth twin of fluid_capture.fs for the real-geometry path (DrawSphereEx).
 * Real spheres DO have back faces, so the far surface comes from front-face
 * culling at the call site rather than a second analytic root — but the
 * reduction still has to be a MAX, hence the same complement-depth trick as
 * fluid_capture_particle_back.fs. */
void main() {
    gl_FragDepth = 1.0 - gl_FragCoord.z;
    finalColor = vec4(gl_FragCoord.z, 1.0, 0.0, 1.0);
}
