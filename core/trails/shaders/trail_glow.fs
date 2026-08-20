#version 330 core
#include "core/shaders/common/vfx_composite.glsl"

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
uniform float uCoreStrength;  // 0.0 = Không lõi, > 0.0 = Lõi rực sáng HDR

// Noise Dissolve / Erosion Controls (XÉ RÁCH VỆT KHÓI)
uniform float uDissolve;      // Dải [0.0 - 1.0]: Độ tan biến/xé rách (0 = nguyên vẹn, 1 = biến mất)
uniform float uMaskTiling;    // Tiling riêng cho Noise Map
uniform vec3  uBurnColor;     // Màu viền rực sáng nơi bị xé rách (Ví dụ: HDR Orange/Blue)

out vec4 finalColor;

void main() {
    // 1. Tính toán Tiling UV
    float mainTiling = (uTiling > 0.0) ? uTiling : 1.0;
    float maskTiling = (uMaskTiling > 0.0) ? uMaskTiling : 1.0;
    
    vec2 uvMain = fragTexCoord * mainTiling;
    vec2 uvMask = fragTexCoord * maskTiling;
    vec4 texColor;

    // ==========================================
    // BƯỚC 1: FLOW MAP (DISTORTION)
    // ==========================================
    if (uStrength > 0.0) {
        vec2 flow = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;

        float phase0 = fract(u_flowTime * uSpeed);
        float phase1 = fract(u_flowTime * uSpeed + 0.5);
        float blend = abs(phase0 * 2.0 - 1.0);

        vec4 col0 = texture(texture0, uvMain + flow * (phase0 - 0.5) * uStrength);
        vec4 col1 = texture(texture0, uvMain + flow * (phase1 - 0.5) * uStrength);
        texColor = mix(col0, col1, blend);
        
        // Biến dạng luôn cả Noise Map theo Flow Map để vết rách cuộn tự nhiên
        uvMask += flow * (phase0 - 0.5) * (uStrength * 0.5);
    } else {
        texColor = texture(texture0, uvMain);
    }

    // ==========================================
    // BƯỚC 2: NOISE DISSOLVE / EROSION (XÉ RÁCH)
    // ==========================================
    float noiseVal = texture(maskTex, uvMask).r; // Lấy độ nhiễu kênh Red
    float alphaMask = 1.0;
    vec3 burnGlow = vec3(0.0);

    if (uDissolve > 0.0) {
        // Cắt bớt Alpha dựa trên Noise Map và uDissolve
        // Đuôi vệt khói sẽ bị xé thành các vệt lởm chởm
        float dissolveThreshold = uDissolve;
        
        // Tạo mặt nạ đục/trong suốt sắc nét
        alphaMask = smoothstep(dissolveThreshold, dissolveThreshold + 0.12, noiseVal);

        // Tạo hiệu ứng viền cháy HDR (Edge Burn) ngay ranh giới xé rách
        float edge = smoothstep(dissolveThreshold - 0.05, dissolveThreshold, noiseVal) - alphaMask;
        // Smoke supplies no burn colour: erosion should make soft gaps, not
        // create an accidental orange energy rim. Effects that want a hot edge
        // must opt in by setting uBurnColor explicitly.
        burnGlow = edge * max(uBurnColor, vec3(0.0)) * 2.5;
    }

    // ==========================================
    // BƯỚC 3: GLOW & HDR CORE (LÕI SÁNG)
    // ==========================================
    float centerDist = abs(fragTexCoord.x - 0.5) * 2.0;
    float glowMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 1.25);

    // Alpha tổng hợp (kết hợp Noise Dissolve)
    float smokeAlpha = texColor.a * fragColor.a * glowMask * alphaMask;

    vec3 finalRGB = (fragColor.rgb * texColor.rgb * 1.5) + burnGlow;
    float finalAlpha = smokeAlpha;

    // Lõi rực sáng ở giữa
    if (uCoreStrength > 0.0) {
        float coreMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 5.5) * uCoreStrength;
        float coreAlpha = clamp(fragColor.a * 1.6, 0.0, 1.0) * alphaMask;

        finalAlpha = mix(smokeAlpha, coreAlpha, coreMask);

        vec3 coreColor = vec3(3.6, 3.6, 3.6); // White HDR Overbright
        finalRGB = mix(finalRGB, coreColor, coreMask);
    }

    // A trail picks BLEND_ALPHA or BLEND_ADDITIVE per instance (trail_system.c
    // maps a premultiplied appearance onto "alpha body + additive emission", so
    // those two are the only outcomes), which is why this is the permutation
    // form rather than a fixed resolver. The variant is chosen by the OUTPUT_*
    // define ResolveShader asked the loader for.
    finalColor = VFX_ResolveOutput(finalRGB, 1.0, finalAlpha,
                                   finalRGB, 1.0, 1.0);
}
