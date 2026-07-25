#version 330

// Đợt E / E2 — spell/effect point lights. `fragWorldPos` here is already in
// the lights' space: these are IMMEDIATE-MODE draws whose vertices rlgl has
// CPU-transformed by RLGL.State.transform (the view matrix) before arrival,
// exactly as ground_shadow.vs documents — so no conversion, the same reason
// ground_shadow.c folds inverse(view) into its shadow matrix.
#include "core/shaders/common/vfx_lights.glsl"

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

// PCF softening. RADIUS = kernel half-size (3 -> 7x7), STEP = texels between taps.
// KEEP STEP == 1.0: contiguous taps = a true box blur = smooth. A STEP > 1 leaves GAPS
// between taps that, at the raking sun angle, beat against the coarse projected texel grid
// and produce the diagonal BANDING seen before. For a softer edge raise RADIUS (more taps),
// never STEP. Map stays 2048 so the projected texels are fine enough to read as smooth.
#define GROUND_SHADOW_PCF_RADIUS 3      // 7x7 kernel (~6cm soft edge at 2048); was 3x3 hard
#define GROUND_SHADOW_PCF_STEP   1.0    // contiguous — no banding

const vec2 poissonDisk[16] = vec2[]( 
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ), 
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ), 
   vec2( -0.91588401, 0.45771432 ), vec2( -0.81544232, -0.87912464 ), 
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ), 
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ), 
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ), 
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ), 
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 ) 
);

// One bilinear-weighted PCF tap: compare the 4 texels around `uv`, then blend the COMPARISON
// RESULTS with the sub-texel fraction. Order matters — comparing first and blending after is what
// removes the texel grid. The map is sampled POINT (env_shadow.c forces it: R32F linear filtering
// is an OPTIONAL format feature and cannot be assumed on either GL3.3 or Vulkan/Mali), so every
// raw tap snaps to a texel centre; that snapping is precisely why the shadow edge showed square
// blocks on device. Doing the interpolation here needs no format feature at all.
float ShadowTapBilinear(vec2 uv, float z) {
    float res  = 1.0 / u_shadowTexel;
    vec2  t    = uv * res - 0.5;
    vec2  f    = fract(t);
    vec2  base = (floor(t) + 0.5) * u_shadowTexel;
    float s00 = step(z, texture(texture0, base).r);
    float s10 = step(z, texture(texture0, base + vec2(u_shadowTexel, 0.0)).r);
    float s01 = step(z, texture(texture0, base + vec2(0.0, u_shadowTexel)).r);
    float s11 = step(z, texture(texture0, base + vec2(u_shadowTexel, u_shadowTexel)).r);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

// Percentage-closer-filtering; returns 1.0 = fully lit, 0.0 = fully shadowed.
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
    float shadow = 0.0;   // (filterRadius/poissonDisk are unused now — see the tap grid below)
    
    // 2x2 grid of BILINEAR taps, 1.5 texels apart = 16 texture fetches — the same fetch budget as
    // the old 16-tap Poisson set, but each fetch now carries a bilinear weight instead of snapping
    // to a grid cell. That is the fix for "the edge shows square blocks" (on-device, Mali-G68):
    // the problem was never the number of samples, it was that POINT sampling quantised every one
    // of them onto the same texel centres. The Poisson set above is kept for reference; a
    // scattered disk is the wrong tool when the map is point-sampled — it leaves gaps that read as
    // separate blocks, and thinning it to 8 taps (tried 2026-07-22) made that clearly worse.
    float z = proj.z - bias;
    const float S = 1.5;
    shadow += ShadowTapBilinear(proj.xy + vec2(-S, -S) * u_shadowTexel, z);
    shadow += ShadowTapBilinear(proj.xy + vec2( S, -S) * u_shadowTexel, z);
    shadow += ShadowTapBilinear(proj.xy + vec2(-S,  S) * u_shadowTexel, z);
    shadow += ShadowTapBilinear(proj.xy + vec2( S,  S) * u_shadowTexel, z);

    return shadow * 0.25;
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
    vec3 floorLit = fragColor.rgb * mix(0.30, 1.0, shadow);
    // A floor plate faces up and carries no vertex normal (immediate-mode draws
    // feed position + colour only), so the flat variant is the only option here.
    floorLit += VFXLights_AccumulateFlat(fragWorldPos, fragColor.rgb);
    finalColor = vec4(floorLit, fragColor.a);
}
