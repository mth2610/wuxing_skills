#version 330
#include "core/shaders/common/fs_header.glsl"

// FRESNEL PROBE — is |N.V| computed correctly in this engine, at all?
//
// Every fresnel that ships here — plasma_shell, crystal, aura_shell,
// effect_material, water_splash — writes `normalize(viewPos - fragPosition)`.
// That is correct only if BOTH operands are in the same space, and two separate
// facts put that in doubt:
//
//   * `viewPos` is auto-bound by SkillManager_BeginShader() and by nothing
//     else. A shader drawn through raw BeginShaderMode() gets (0,0,0), so the
//     "view direction" points at the WORLD ORIGIN.
//   * `fragPosition` comes from matModel, and inside a 3D pass MyBeginMode3D
//     folds matView into rlgl's transform, so matModel is model x view
//     (ENGINE_LANDMINES §9) and fragPosition is VIEW space.
//
// If both hold, the standard line subtracts a view-space point from a
// world-space one and every fresnel in the project has been wrong. That is a
// large claim, so this shader computes BOTH readings and the harness measures
// which one behaves like a fresnel actually should.
//
// GROUND TRUTH. On a cylinder seen from the side, |N.V| must be 1 along the
// centre line and fall to 0 at both silhouettes, symmetrically. Anything else —
// asymmetric, flat, or peaking off-centre — is a broken convention. That test
// needs no art direction and no opinion.

// 0 = world reading:  normalize(viewPos - fragPosition)
// 1 = view reading:   normalize(-fragPosition)
// 2 = length(fragNormal) — DOES THE ATTRIBUTE ARRIVE AT ALL?
// 3 = length(fragPosition) / 20.0 — fs_header.glsl's own decisive test: this
//     must read as the distance from the object to the WORLD ORIGIN if
//     fragPosition is world space, or to the CAMERA if it is view space. The
//     harness knows both expected distances (computed in C from the same
//     probe/camera positions) and logs them alongside this readback — no
//     dome-shape inference needed, just two numbers that cannot tie.
//
// Mode 2 exists because both fresnel readings came back as a monotone ramp
// across the body rather than a dome, which is the signature of dot(constant,
// V): N not varying. The cylinder's own code writes a correct per-vertex radial
// normal (pm_core_shapes.inl), so if the shader is not seeing one, the loss is
// between the vertex buffer and here. No normalize, no dot — this cannot go NaN
// and cannot be confused with a numerical accident.
//
// 4 = a LITERAL constant (u_refValue), no scene math at all. Mode 3's readback
// (216) had to be judged against distWorld/20 and distCam/20 by mentally
// inverting ACES + display gamma — guesswork. Two mode-4 markers, drawn with
// EXACTLY those two f values as plain literals, go through the identical
// tonemap/grade the cylinder does; comparing three numbers that all passed
// through the same unknown curve needs no knowledge of that curve's shape.
uniform float u_probeMode;
uniform float u_refValue;

void main()
{
    float f;
    if (u_probeMode > 3.5) {
        f = clamp(u_refValue, 0.0, 1.0);
    } else if (u_probeMode > 2.5) {
        f = clamp(length(fragPosition) / 20.0, 0.0, 1.0);
    } else if (u_probeMode > 1.5) {
        f = clamp(length(fragNormal), 0.0, 1.0);
    } else {
        vec3 N = normalize(fragNormal);
        vec3 V = (u_probeMode < 0.5) ? normalize(viewPos - fragPosition)
                                     : normalize(-fragPosition);
        f = clamp(abs(dot(N, V)), 0.0, 1.0);
    }
    // Greyscale and fully opaque: the harness reads the red channel back as a
    // number, so nothing may modulate it. No blending, no tint, no texture.
    finalColor = vec4(f, f, f, 1.0);
}
