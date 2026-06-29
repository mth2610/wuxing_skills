#version 330

// ============================================================
// Core Test Skill — Fragment Shader
//
// Mục đích: Kiểm tra tất cả common shader functions mới.
//
// [TEST] noise.glsl   → fbm2(), fbm2N(), hash3()
// [TEST] lighting.glsl → calcDiffuse() (mới), calcFresnel(),
//                        calcSpecular(), perturbNormal()
// [TEST] fx.glsl      → dissolveCalc(), emissiveMask()
// ============================================================

#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

uniform float u_dissolve;  // [0..1] — tăng khi gần hết lifetime
uniform float u_phase;     // [0..1] — tiến trình sinh tồn

void main() {
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));  // hướng đèn chuẩn project
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 normal   = normalize(fragNormal);

    // ── [TEST noise.glsl] fbm2 — 3-octave FBM, bề mặt turbulence ──
    float surf = fbm2(fragPosition.xz * 0.045 + u_time * 0.22);

    // ── [TEST noise.glsl] fbm2N — 5-octave FBM, chi tiết mịn hơn ──
    float fine = fbm2N(fragPosition.xz * 0.09 - vec2(0.0, u_time * 0.15), 5);

    // ── [TEST lighting.glsl] perturbNormal — bề mặt gồ ghề từ fbm ──
    const float eps = 0.04;
    vec2 hDelta = vec2(
        fbm2((fragPosition.xz + vec2(eps, 0.0)) * 0.045 + u_time * 0.22) -
        fbm2((fragPosition.xz - vec2(eps, 0.0)) * 0.045 + u_time * 0.22),
        fbm2((fragPosition.xz + vec2(0.0, eps)) * 0.045 + u_time * 0.22) -
        fbm2((fragPosition.xz - vec2(0.0, eps)) * 0.045 + u_time * 0.22)
    );
    vec3 pertNormal = perturbNormal(normal, hDelta, 0.45);

    // ── [TEST lighting.glsl] calcDiffuse (hàm mới) ──
    float diff    = calcDiffuse(pertNormal, lightDir, 0.12);

    // ── [TEST lighting.glsl] calcFresnel + calcSpecular (cũ, kiểm tra vẫn hoạt động) ──
    float fresnel = calcFresnel(pertNormal, viewDir, 2.8);
    float spec    = calcSpecular(pertNormal, lightDir, viewDir, 192.0);

    // ── [TEST fx.glsl] emissiveMask — mạch năng lượng chạy trên orb ──
    // Tần số dao động theo surf để mạch "hô hấp" theo turbulence bề mặt
    float vein = emissiveMask(fragPosition, 1.1 + surf * 0.6, 0.84);

    // ── Tổng hợp màu Taiji ──
    vec3 coreColor  = vec3(0.20, 0.03, 0.42);
    vec3 rimColor   = vec3(0.65, 0.18, 1.00);
    vec3 baseColor  = mix(coreColor, rimColor, surf * 0.55 + fine * 0.25);
    baseColor *= diff;

    // Rim glow (Fresnel)
    baseColor += vec3(0.50, 0.15, 1.00) * fresnel * 0.55;

    // Specular trắng tím
    baseColor += vec3(0.92, 0.75, 1.00) * spec * 3.5;

    // Emissive mạch năng lượng (tím vàng ánh lên theo vein mask)
    baseColor += vec3(0.90, 0.55, 1.00) * vein * 3.0;

    // ── [TEST fx.glsl] dissolveCalc — discard + viền vàng cháy ──
    // hash3 từ noise.glsl để tính noiseVal per-fragment
    float n = hash3(floor(fragPosition * 7.5));
    float edgeFactor;
    if (dissolveCalc(n, u_dissolve, 0.10, edgeFactor) >= 1.0) discard;
    // Viền cháy màu vàng gold khi dissolve
    vec3 edgeGlow = vec3(1.00, 0.82, 0.18) * 4.5;
    baseColor = mix(baseColor, edgeGlow, edgeFactor);

    // Alpha: mờ ở tâm, rõ ở rìa (Fresnel alpha) — giảm khi dissolving
    float alpha = mix(0.30, 0.92, fresnel) * (1.0 - u_dissolve * 0.5);

    finalColor = vec4(baseColor, clamp(alpha, 0.0, 1.0));
}
