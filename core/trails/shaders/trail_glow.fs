#version 330 core
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/lighting.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

// ==========================================
// TEXTURE SLOTS
// ==========================================
uniform sampler2D texture0;   // Slot 0: Main Trail Texture
uniform sampler2D flowTex;    // Slot 1: Flow / Direction Map
uniform sampler2D maskTex;    // Slot 2: Noise / Dissolve Mask Map

// ==========================================
// UNIFORMS
// ==========================================
// Flow Map Controls
uniform float u_flowTime;
uniform float uSpeed;
uniform float uStrength;      // = 0.0 nếu không dùng Flow Map
uniform float uTiling;

// Glow & HDR Core Controls
uniform float u_coreStrength; // 0.0 = Không lõi, > 0.0 = Lõi rực sáng HDR

// Noise Dissolve / Erosion Controls (XÉ RÁCH VỆT KHÓI)
uniform float uDissolve;      // Dải [0.0 - 1.0]: Độ tan biến/xé rách (0 = nguyên vẹn, 1 = biến mất)
uniform float uMaskTiling;    // Tiling riêng cho Noise Map
uniform vec3  uBurnColor;     // Màu viền rực sáng nơi bị xé rách (Ví dụ: HDR Orange/Blue)

out vec4 finalColor;

void main() {
    if (fragColor.a <= 0.001) discard;

    // 1. Tính toán Tiling UV
    float mainTiling = (uTiling > 0.0) ? uTiling : 1.0;
    vec2 uvMain = fragTexCoord * mainTiling;
    vec4 texColor;
    vec2 flowOffset = vec2(0.0);

    // ==========================================
    // BƯỚC 1: FLOW MAP (DISTORTION)
    // ==========================================
    if (uStrength > 0.0) {
        vec2 flow = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;

        float phase0 = fract(u_flowTime * uSpeed);
        float phase1 = fract(u_flowTime * uSpeed + 0.5);
        float blend = abs(phase0 * 2.0 - 1.0);

        flowOffset = flow * (phase0 - 0.5) * uStrength;
        vec4 col0 = texture(texture0, uvMain + flowOffset);
        vec4 col1 = texture(texture0, uvMain + flow * (phase1 - 0.5) * uStrength);
        texColor = mix(col0, col1, blend);
    } else {
        texColor = texture(texture0, uvMain);
    }

    // ==========================================
    // BƯỚC 2: NOISE DISSOLVE / EROSION (XÉ RÁCH)
    // ==========================================
    float alphaMask = 1.0;
    vec3 burnGlow = vec3(0.0);

    if (uDissolve > 0.0) {
        float maskTiling = (uMaskTiling > 0.0) ? uMaskTiling : 1.0;
        vec2 uvMask = fragTexCoord * maskTiling + flowOffset * 0.5;
        float noiseVal = texture(maskTex, uvMask).r;

        float dissolveThreshold = uDissolve;
        alphaMask = smoothstep(dissolveThreshold, dissolveThreshold + 0.12, noiseVal);

        // Chỉ tính viền cháy HDR (Edge Burn) khi người dùng thực sự cấu hình uBurnColor > 0.
        // Sửa lỗi: khi uBurnColor = (0,0,0), step(0.0, 0.0) trả về 1.0 khiến khói bị viền cam/trắng.
        if (dot(uBurnColor, uBurnColor) > 1e-4) {
            float edge = smoothstep(dissolveThreshold - 0.05, dissolveThreshold, noiseVal) - alphaMask;
            if (edge > 1e-4) {
                float isWarm = step(uBurnColor.b, uBurnColor.r * 0.85);
                vec3 radiantBurn = calcRadiantEnergy(edge, max(uBurnColor, vec3(0.0)), vec3(1.0, 0.98, 0.92), isWarm, 2.5);
                burnGlow = edge * radiantBurn;
            }
        }
    }

    // ==========================================
    // BƯỚC 3: GLOW & HDR CORE (LÕI SÁNG)
    // ==========================================
    float centerDist = abs(fragTexCoord.x - 0.5) * 2.0;
    float x = clamp(1.0 - centerDist, 0.0, 1.0);
    // Xấp xỉ chính xác x^1.25 = x * sqrt(sqrt(x)) không cần pow()
    float sqrtX = sqrt(x);
    float glowMask = x * sqrt(sqrtX);

    float smokeAlpha = texColor.a * fragColor.a * glowMask * alphaMask;
    vec3 finalRGB = (fragColor.rgb * texColor.rgb * 1.5) + burnGlow;
    float finalAlpha = smokeAlpha;

    // Lõi rực sáng ở giữa (Ghost of Tsushima Radiant Energy Core)
    if (u_coreStrength > 0.0) {
        // x^5.5 = x^5 * sqrt(x) nhanh hơn pow(x, 5.5)
        float x2 = x * x;
        float coreMask = (x2 * x2 * x * sqrtX) * u_coreStrength;
        float coreAlpha = clamp(fragColor.a * 1.6, 0.0, 1.0) * alphaMask;

        finalAlpha = mix(smokeAlpha, coreAlpha, coreMask);

        float isWarm = step(fragColor.b, fragColor.r * 0.85);
        vec3 coreColor = calcRadiantEnergy(coreMask, fragColor.rgb, vec3(1.0, 0.98, 0.92), isWarm, 3.6);
        finalRGB = mix(finalRGB, coreColor, coreMask);
    }

    if (finalAlpha < 0.002 && dot(burnGlow, burnGlow) < 1e-5) discard;

    finalColor = VFX_ResolveOutput(finalRGB, 1.0, finalAlpha,
                                   finalRGB, 1.0, 1.0);
}
