#version 330 core
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/noise.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D noiseTex;
uniform vec4 colDiffuse;

uniform float u_time;
uniform float u_noiseScale;     // Tần số cuộn dải
uniform float u_speed1;         // Tốc độ trôi dải 1
uniform float u_speed2;         // Tốc độ trôi dải 2
uniform float u_erosionAmount;  // Tiến trình xói mòn [0.0 .. 1.0]
uniform float u_softness;       // Biên độ mềm
uniform vec4  u_windColor;      // Màu luồng gió

out vec4 finalColor;

void main() {
    float x = fragTexCoord.x;
    float y = fragTexCoord.y;

    // ── 1. BIẾN DẠNG CHẤT LƯU KHÍ QUYỂN (Flow Distortion từ kênh B của texture0) ──
    // Kênh B là trường xoáy chất lưu hai chiều (zero-mean flow distortion)
    float b1 = texture(texture0, vec2(x * 0.5 + 0.25, fract(y * 1.5 - u_time * 1.2))).b - 0.5;
    float b2 = texture(texture0, vec2(x * 0.8 + 0.10, fract(y * 2.2 - u_time * 1.8))).b - 0.5;
    float flow = (b1 + b2 * 0.6) * 0.07;
    float curl = (vnoise(vec2(x * 2.5, y * 2.0 - u_time * 1.5)) - 0.5) * 0.025;
    float totalWarp = flow + curl;

    // ── 2. CẤU TRÚC 3 SỢI KHÓI CUỘN CHẢY (RzFX Sin-Wave Strand Fields: w0, w1, w2) ──
    // Sợi giữa (w0): Uốn lượn quanh trục giữa (x ~ 0.50)
    float w0 = 0.50 + sin(y * 6.28318 - u_time * 2.0) * 0.035 + cos(y * 12.56636 + u_time * 1.2) * 0.015;

    // Sợi trái (w1): Tần số & pha lệch trục, tự do đan dệt
    float w1 = 0.35 + sin(y * 9.42477 - u_time * 2.6 + 1.2) * 0.040 + cos(y * 5.0 - u_time * 1.5) * 0.018;

    // Sợi phải (w2): Tần số & pha lệch trục so le
    float w2 = 0.65 + sin(y * 8.5 - u_time * 2.3 + 3.5) * 0.040 - cos(y * 6.0 - u_time * 1.7) * 0.018;

    float bundleHW = 0.055;

    // SỢI GIỮA (Đậm hơn xíu, thanh thoát, lấy mẫu khói từ kênh R)
    float d0 = abs(x - w0 - totalWarp) / bundleHW;
    float env0 = exp(-d0 * d0 * 1.8);
    float v0 = fract(y * 1.2 - u_time * 1.8);
    float u0 = clamp(0.5 + (x - w0 - totalWarp) * 3.0, 0.0, 1.0);
    float texWisp0 = texture(texture0, vec2(u0, v0)).r;
    float strand0 = env0 * (0.28 + 0.50 * texWisp0);
    float core0 = exp(-pow(abs(x - w0 - totalWarp) / 0.011, 2.0));

    // SỢI TRÁI (Mờ hơn, thanh thoát, đứt nhẹ tự nhiên từ kênh G)
    float d1 = abs(x - w1 - totalWarp * 0.8) / (bundleHW * 0.85);
    float env1 = exp(-d1 * d1 * 1.8);
    float v1 = fract(y * 1.6 - u_time * 2.4);
    float u1 = clamp(0.5 + (x - w1 - totalWarp * 0.8) * 3.5, 0.0, 1.0);
    float texWisp1 = texture(texture0, vec2(u1, v1)).g;
    float strand1 = env1 * (0.35 + 0.55 * texWisp1) * 0.42;

    // SỢI PHẢI (Mờ hơn, so le đứt nhẹ từ kênh R)
    float d2 = abs(x - w2 - totalWarp * 0.8) / (bundleHW * 0.85);
    float env2 = exp(-d2 * d2 * 1.8);
    float v2 = fract(y * 1.4 - u_time * 2.1 + 0.5);
    float u2 = clamp(0.5 + (x - w2 - totalWarp * 0.8) * 3.5, 0.0, 1.0);
    float texWisp2 = texture(texture0, vec2(u2, v2)).r;
    float strand2 = env2 * (0.35 + 0.55 * texWisp2) * 0.40;

    // ── 3. HAI TẦNG GIAO THOA KHÍ ĐỘNG HỌC (Dual-layer smoke shear) ──
    float s1 = max(strand0, max(strand1, strand2));

    // Tầng 2: Cuộn xé vi sai (shear) ở vận tốc khác
    float v0_alt = fract(y * 1.0 - u_time * 2.5);
    float texWisp0_alt = texture(texture0, vec2(u0, v0_alt)).g;
    float d0_alt = abs(x - w0 + totalWarp * 0.7) / (bundleHW * 1.15);
    float strand0_alt = exp(-d0_alt * d0_alt * 1.8) * (0.35 + 0.45 * texWisp0_alt);
    float strand1_alt = env1 * texWisp1 * 0.32;
    float strand2_alt = env2 * texWisp2 * 0.30;
    float s2 = max(strand0_alt, max(strand1_alt, strand2_alt));

    // Giao thoa 2 tầng lụa khí vi phân (giữ nguyên token contract)
    float silk = mix(s1, s2, 0.48);

    // ── 4. XÓI MÒN & ĐỨT QUÃNG KHÓI TRÔI (Kênh A Dissolve & Nhiễu xé sợi) ──
    float dis = texture(texture0, vec2(x * 0.8 + 0.1, fract(y * 1.3 - u_time * 0.9))).a;
    float disCurl = (vnoise(vec2(x * 3.0, y * 2.5 - u_time * 1.4)) - 0.5) * 0.08;
    float soft = 0.22;
    float baseThreshold = mix(0.24, 0.12, y) + u_erosionAmount * 0.22;
    float threshold = baseThreshold + (dis - 0.5) * 0.26 + disCurl;
    float alphaErode = smoothstep(threshold - soft, threshold + soft, silk);

    // ── 5. TRIỆT TIÊU MÉP LƯỚI HÌNH HỌC (Ghost of Tsushima Edge Attenuation: 4.0 * V * (1.0 - V)) ──
    float edgeMask = clamp(4.0 * x * (1.0 - x), 0.0, 1.0);
    edgeMask = pow(edgeMask, 1.25);

    // ── 6. VUỐT NHỌN MŨI KIM Ở ĐẦU VÀ TIÊU TAN Ở ĐUÔI ──
    float headFade = smoothstep(1.0, 0.92, y);
    float tailFade = smoothstep(0.0, 0.18, y);
    float lengthFade = headFade * tailFade;

    // ── 7. PHÂN CẤP ÁNH SÁNG & MÀU SẮC ──
    vec4 baseColor = (u_windColor.a > 0.01) ? u_windColor : vec4(0.90, 0.95, 1.0, 0.55);
    vec3 mistColor = baseColor.rgb * colDiffuse.rgb * fragColor.rgb;
    vec3 coreWhite = vec3(0.98, 1.0, 1.0);

    // Điểm nhấn sợi giữa đậm hơn xíu với lõi sáng tinh tế (không bị gắt)
    float centerGlow = core0 * smoothstep(0.40, 0.72, silk);
    vec3 color = mix(mistColor, coreWhite, centerGlow * 0.45);

    // Alpha tổng hợp: Sợi giữa đậm hơn xíu, 2 sợi cạnh mờ thanh thoát
    float strandAlpha = alphaErode * (0.40 + centerGlow * 0.26);
    float totalAlpha = clamp(strandAlpha * edgeMask * lengthFade * baseColor.a * colDiffuse.a * fragColor.a * 1.35, 0.0, 1.0);

    if (totalAlpha < 0.003) {
        discard;
    }

    finalColor = VFX_ResolveBody(color, 1.0, totalAlpha);
}
