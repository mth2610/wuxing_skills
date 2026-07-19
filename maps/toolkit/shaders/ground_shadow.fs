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

out vec4 finalColor;

// 3x3 PCF; returns 1.0 = fully lit, 0.0 = fully shadowed.
float ShadowFactor(vec3 worldPos) {
    // vec * mat (== transpose(mat) * vec): u_lightVP arrives transposed via
    // SetShaderValueMatrix under rlvk (raylib's internal mvp upload transposes,
    // SetShaderValueMatrix does not), so this is the multiply that produces
    // correct light-space coordinates. Confirmed via a bucketed proj.z debug.
    vec4 posLS = vec4(worldPos, 1.0) * u_lightVP;
    vec3 proj = posLS.xyz / posLS.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float bias = 0.0015;
    vec2 texel = 1.0 / vec2(textureSize(texture0, 0));
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
    // STATUS (2026-07-19): NOT working yet — the two fixes above (texture0
    // binding + vec*mat) are confirmed correct (proj.z now shows a healthy
    // depth gradient), but the SAMPLED depth is still ~1.0 everywhere, so no
    // fragment tests as occluded. Root cause: the CAPTURE pass stores depth
    // crammed at the far plane (casters barely below 1.0). See
    // REAL_SHADING_P6_NOTES.md before resuming — the remaining bug is in
    // EnvShadow_BeginCapture's projection/depth write, not here.
    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }
    // Darken toward ~35%, not full black — reads as a soft contact shadow on
    // an already-dark night floor instead of a hole.
    finalColor = vec4(fragColor.rgb * mix(0.35, 1.0, shadow), fragColor.a);
}
