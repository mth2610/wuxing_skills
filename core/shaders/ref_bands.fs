#version 330

// REFERENCE BANDS — the most direct write into the scene target this codebase has.
//
// WHY IT IS THIS EMPTY. Every conclusion about the HDR pipeline until now was
// inferred from ART: measure a flame, reason backwards. But an art asset can be
// wrong in the same direction as the pipeline, and then the two agree and both
// are wrong. This shader removes the asset from the question — no texture, no
// gradient, no noise, no time, no normals, no fragColor multiply. The authored
// number goes to the framebuffer and nothing may touch it on the way.
//
// So the contract is exact and checkable: write N, and the scene target must
// contain N. Anything else is the pipeline, and there is nowhere else to look.

uniform vec3 u_radiance;   // scene-referred, may exceed 1.0 — that is the point

out vec4 finalColor;

void main() {
    // Alpha 1.0 by the same rule distortion.fs follows: whatever writes the
    // scene target defines its own alpha rather than leaving it undefined
    // (core/scene_targets.h).
    finalColor = vec4(u_radiance, 1.0);
}
