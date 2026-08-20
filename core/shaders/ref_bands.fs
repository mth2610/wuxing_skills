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
// Coverage, which selects WHICH blend law this patch is a reference for:
//   1.0  additive row      — rgb goes in whole, alpha is irrelevant to the blend
//   a<1  premultiplied row — rgb is pre-scaled by a, so the hardware computes
//                            src + dst*(1-a), i.e. §5.2's law, and the expected
//                            scene value is exactly radiance*a + background*(1-a)
uniform float u_coverage;

out vec4 finalColor;

// DELIBERATELY NOT on VFX_ResolveOutput, unlike every other producer (M1,
// 20/08/2026). This patch exists to measure the compositing pipeline, so it
// must write the exact values the two blend laws expect and nothing else. Route
// it through the shared resolver and the instrument starts measuring the
// resolver too — a bug in VFX_Resolve* would then be invisible in the one place
// built to see it. Same reason probe_gradient.fs and probe_fresnel.fs stay out.
void main() {
    // Alpha 1.0 by the same rule distortion.fs follows: whatever writes the
    // scene target defines its own alpha rather than leaving it undefined
    // (core/scene_targets.h).
    finalColor = vec4(u_radiance * u_coverage, u_coverage);
}
