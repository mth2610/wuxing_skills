#version 330
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_lights.glsl"

// ============================================================
// WUXING — grass_material Fragment Shader (Optimized & Visual Upgraded)
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

// Tinh chỉnh hiệu ứng Visual
#define GRASS_EDGE_BREAKUP 0.60   // Độ xé lẻ rìa cỏ/đất theo hạt chi tiết
#define GRASS_RIM_DARKEN   0.18   // Độ tối viền đất ẩm nơi tiếp giáp gốc cỏ
#define GRASS_LUSH_SHIFT   0.12   // Độ chuyển sắc Xanh non <-> Xanh ngọc theo vùng

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 v_tileUVs;    // .xy = baseUV,      .zw = detailUV
in vec4 v_noiseUVs;   // .xy = maskNoiseUV, .zw = colorVarUV

uniform sampler2D texture0;   // grassBase
uniform sampler2D texture2;   // grassDetail (grayscale grain ~0.5)
uniform sampler2D texture3;   // dirt

uniform mediump vec4 colDiffuse;

// Giữ nguyên khai báo uniform để tương thích 100% với core/grass_material.c
uniform float u_baseTileSize;
uniform float u_detailTileSize;
uniform float u_maskNoiseScale;
uniform float u_colorVarScale;

uniform vec3 u_lightDir;
uniform mediump vec3 u_lightColor;
uniform mediump vec3 u_ambientColor;

out vec4 finalColor;

// Hàm biến thiên vùng rộng (Domain-warped sine): Nhanh gấp 4 lần fbm2 nhưng
// tạo độ loang màu tự nhiên, không bị lặp lưới cho cả PC và Mobile.
float broadVariation(vec2 p) {
    p *= 3.14159265;
    float a = sin(p.x + 1.7 * sin(p.y * 0.71 + 1.3));
    float b = sin(p.y * 1.13 + 1.9 * sin(p.x * 0.57 + 4.1));
    return 0.5 + 0.25 * (a + b);
}

void main() {
    vec2 baseUV   = v_tileUVs.xy;
    vec2 detailUV = v_tileUVs.zw;

    // 1. Đọc hạt chi tiết (detail) và tính mặt nạ (mask) TRƯỚC
    mediump float detail = texture(texture2, detailUV).r;
    mediump float grainOffset = detail - 0.5;

    float n = fbm2(v_noiseUVs.xy);
    mediump float edge = n + grainOffset * GRASS_EDGE_BREAKUP;
    mediump float mask = smoothstep(0.40, 0.50, edge);

    // 2. Lấy mẫu Cỏ và áp dụng chi tiết hạt
    mediump vec3 grass = texture(texture0, baseUV).rgb;
    mediump vec3 grassDetailed = grass * (1.0 + 0.30 * grainOffset);

    // 3. VISUAL UPGRADE: Chuyển sắc tự nhiên theo vùng (Broad Color & Hue Shift)
    // Vùng colorVar cao: Cỏ ngả vàng ấm (nắng/khô). Vùng thấp: Cỏ xanh đậm mát (ẩm).
    mediump float colorVar = broadVariation(v_noiseUVs.zw);
    mediump float varCentered = colorVar - 0.5;
    mediump vec3 hueTint = vec3(
        1.0 + varCentered * GRASS_LUSH_SHIFT,         // Red: ấm hơn ở vùng sáng
        1.0 + varCentered * (GRASS_LUSH_SHIFT * 0.5), // Green: giữ độ tươi
        1.0 - varCentered * GRASS_LUSH_SHIFT          // Blue: mát hơn ở vùng tối
    );
    grassDetailed *= hueTint * (1.0 + 0.28 * varCentered);

    // 4. TỐI ƯU BĂNG THÔNG: Chỉ đọc Texture Đất (texture3) khi pixel thực sự có đất!
    // Đa số diện tích mặt đất là cỏ (mask == 0), nhánh này giúp GPU bỏ qua lượt fetch texture3
    mediump vec3 blended = grassDetailed;
    if (mask > 0.001) {
        mediump vec3 dirt = texture(texture3, baseUV).rgb;
        
        // VISUAL UPGRADE: Tận dụng luôn hạt detail để tạo độ nhám sỏi cát cho Đất
        dirt *= (1.0 + 0.22 * grainOffset) * (0.92 + 0.16 * colorVar);

        // Viền đất ẩm tối màu ở ranh giới Cỏ - Đất (đạt đỉnh tại mask = 0.5)
        mediump float rim = mask * (1.0 - mask) * 4.0;
        
        // Tạo cảm giác gờ cỏ cao hơn mặt đất nhờ làm dịu nhẹ ánh sáng ở mép đất
        blended = mix(grassDetailed, dirt, mask);
        blended *= 1.0 - (GRASS_RIM_DARKEN * rim);
    }

    // 5. Tính toán Ánh sáng (Lighting)
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(u_lightDir);

    // VISUAL UPGRADE: Cỏ có lá mỏng tán xạ ánh sáng (Subsurface Wrap = 0.15)
    // trong khi Đất phẳng lì (Wrap = 0.0). Pha trộn độ mở sáng theo mask.
    mediump float wrapFactor = mix(0.15, 0.0, mask);
    mediump float diff = calcDiffuse(normal, lightDir, wrapFactor);

    mediump vec3 lit = blended * (u_ambientColor + diff * u_lightColor);
    lit += VFXLights_Accumulate(fragPosition, normal, blended);

    finalColor = vec4(lit, 1.0) * colDiffuse;
}