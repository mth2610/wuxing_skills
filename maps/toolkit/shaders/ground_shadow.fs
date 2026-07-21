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
    // mat*vec — TEST-PROVEN by rlvk visual scenario `shadow_proj` (2026-07-21):
    // for a CUSTOM `uniform mat4` uploaded via SetShaderValueMatrix, `M * v`
    // reproduces the CPU light-space projection to 0.002, while `v * M` is off
    // by 0.324 (it lands the shadow at the wrong UV → the "shadow drifts far
    // from the caster" screenshots). Run: scripts/run_rlvk_visual_test.sh
    // shadow_proj. Do NOT flip this from in-game guessing — the scenario is the
    // arbiter; if it ever fails, the rlvk mat4 decoration changed.
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

// TEMP DEBUG (2026-07-21): paints the whole floor with the shadow PROJECTION
// FIELD so we can SEE, in-game, exactly what the ground computes — the answer
// to "where is the shadow being drawn?". Reading the colors:
//   * smooth RED→GREEN gradient across the floor = u_lightVP works, proj is
//     sane; the BLACK silhouette (sampled occluder depth) shows where the
//     shadow lands — that spot IS the shadow's coordinate.
//   * whole floor BLUE = proj falls OUTSIDE [0,1] everywhere → u_lightVP is
//     wrong/garbage in-game (uniform not reaching the FS).
//   * flat uniform grey, no black shape, no blue = texture0 is NOT the shadow
//     map (sampling the default white) → binding failed.
// Set to 0 to restore the normal darken. Convention is M*v, TEST-PROVEN
// (scripts/run_rlvk_visual_test.sh shadow_proj / shadow_cast).
#define GROUND_SHADOW_DEBUG_PROJ 0

void main() {
#if GROUND_SHADOW_DEBUG_PROJ
    if (u_shadowEnabled > 0.5) {
        vec4 p = u_lightVP * vec4(fragWorldPos, 1.0);
        vec3 pr = p.xyz / p.w * 0.5 + 0.5;
        if (pr.x < 0.0 || pr.x > 1.0 || pr.y < 0.0 || pr.y > 1.0) {
            finalColor = vec4(0.0, 0.0, 0.4, 1.0);      // dim BLUE: outside the shadow frustum
            return;
        }
        // Pure sampled shadow-map depth across the floor. This is a BINDING test:
        //   * floor shows a DARK character-shaped patch somewhere = texture0 IS
        //     the shadow map, sampling works -> that patch = the shadow's spot.
        //   * floor is UNIFORM WHITE (no dark patch anywhere, only dim-blue edges)
        //     = texture0 is NOT bound to the shadow map for this immediate-mode
        //     3D draw (rlSetTexture didn't stick) -> the rlvk binding bug.
        float d = texture(texture0, pr.xy).r;
        finalColor = vec4(vec3(d), 1.0);                // WHITE=far(1.0), DARK=occluder
        return;
    }
#endif
    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }
    // Darken toward ~30%, not full black — reads as a soft shadow on the
    // night floor instead of a hole.
    finalColor = vec4(fragColor.rgb * mix(0.30, 1.0, shadow), fragColor.a);
}
