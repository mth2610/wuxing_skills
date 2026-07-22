#version 330

// Real Shading P6 — writes the caster's NDC depth straight into an R32F COLOR
// attachment (perf: this removes the separate depth->R32F copy pass AND the
// rlvk §7.10 depth-sample twin bounce — ~2 fewer render/blit encoders per frame
// on MoltenVK/Intel, the P6 FPS cost). Depth TEST still runs against the FBO's
// depth attachment so the nearest caster wins; only its z is emitted as color.
// This is exactly the proven `shadow_cast` visual-test path. The receiver
// (ground_shadow.fs / surface_lit.fs) compares its own proj.z against this .r.
out vec4 finalColor;

void main() {
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
