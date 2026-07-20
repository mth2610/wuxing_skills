#version 330

// Real Shading P6 — fragment shader for GroundShadow. The ground's "lighting"
// here is pre-baked per-vertex color, so shadow is applied as a straight
// darken-toward-a-floor multiplier.
//
// IMPORTANT (rlvk): the shadow map is sampled through the DEFAULT `texture0`
// sampler, NOT a custom `sampler2D shadowMap` uniform. Under this project's
// Vulkan backend (rlvk), a custom sampler bound via SetShaderValueTexture
// read as an unbound/white texture (~1.0 = far) for immediate-mode 3D draws,
// so no fragment ever tested as occluded. Binding the shadow map as texture0
// via rlSetTexture (the exact path DrawTexture/DrawTexturePro uses — proven
// working, it's what the on-screen debug preview uses) is the reliable route.
// The vertex texcoord is irrelevant here: we sample at the light-space
// PROJECTED coordinate computed from fragWorldPos, not at a mesh UV.

in vec4 fragColor;
in vec3 fragWorldPos;

uniform sampler2D texture0; // shadow depth map, bound via rlSetTexture (see above)
uniform mat4      u_lightVP;
uniform float     u_shadowEnabled;
uniform float     u_shadowTexel; // 1.0/resolution — do NOT use textureSize():
                                 // under rlvk it returned 0 -> texel=INF ->
                                 // all 9 PCF coords NaN (0*INF) -> no shadow.

out vec4 finalColor;

// 3x3 PCF; returns 1.0 = fully lit, 0.0 = fully shadowed.
float ShadowFactor(vec3 worldPos) {
    // mat * vec — CONFIRMED via spirv-dis (RLVK_DUMP_SPV) that rlvk/shaderc's
    // auto-generated default uniform block decorates u_lightVP ColMajor,
    // MatrixStride 16, Offset 0 (glslang's normal default), and
    // rlSetUniformMatrix uploads raylib's Matrix (m0..m15) straight across —
    // i.e. standard GL column-major, matching plain M*v everywhere else in
    // this engine (the automatic `mvp` uniform, surface_lit's other
    // matrices). The earlier "vec*mat" workaround (session 3) computed
    // transpose(u_lightVP)*v instead, which only coincidentally agreed with
    // M*v near the light's aim point (ARENA_CENTER) — off-center it diverges
    // by the transpose of the view's rotation block, growing with distance:
    // this is what caused the reported "shadow position/size drifts
    // proportionally to distance from arena center" bug. See
    // REAL_SHADING_P6_NOTES.md session-4 for the spirv-dis evidence.
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float bias = 0.0015;
    vec2 texel = vec2(u_shadowTexel);
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float pcfDepth = texture(texture0, proj.xy + vec2(float(x), float(y)) * texel).r;
            shadow += (proj.z - bias > pcfDepth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    // STATUS (2026-07-20, session 4 part 11): back to real production logic
    // while the matrix-delivery question is chased on the C side instead
    // (maps/toolkit/ground_shadow.c + environment/env_shadow.c now each
    // TraceLog the exact Matrix bytes at the moment of computation/upload —
    // no shader/pixel-readback diagnostics needed, and it sidesteps a real
    // methodology problem found in parts 9-10: the screen-space pixel scan's
    // values didn't vary smoothly/consistently with world position, which
    // could be HDR bloom bleed from nearby glowing props OR the scan path
    // simply walking across other rendered geometry (sandbox dummies/props)
    // instead of pure ground_shadow.fs pixels — either way, pixel-color
    // encoding of numeric shader state proved unreliable for this precision
    // of comparison and shouldn't be trusted further without ruling that out
    // first. See REAL_SHADING_P6_NOTES.md session-4 part 11.
    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }
    // Darken toward ~30%, not full black — reads as a soft shadow on the
    // night floor instead of a hole.
    finalColor = vec4(fragColor.rgb * mix(0.30, 1.0, shadow), fragColor.a);
}
