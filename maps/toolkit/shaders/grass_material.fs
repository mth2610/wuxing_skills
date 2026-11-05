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

    // [VULKAN / SPIR-V FIX]: Tính trước đạo hàm UV ở luồng đồng nhất (uniform control flow)
    // để dùng cho textureGrad bên trong các nhánh rẽ mà không gây lỗi mipmap trên Vulkan.
    vec2 baseDx = dFdx(baseUV);
    vec2 baseDy = dFdy(baseUV);

    // 1. Đọc hạt chi tiết (detail) và tính mặt nạ (mask) TRƯỚC
    mediump float detail = texture(texture2, detailUV).r;
    mediump float grainOffset = detail - 0.5;

    float n = fbm2(v_noiseUVs.xy);
    mediump float edge = n + grainOffset * GRASS_EDGE_BREAKUP;
    mediump float mask = smoothstep(0.40, 0.50, edge);

    mediump float colorVar = broadVariation(v_noiseUVs.zw);
    mediump vec3 grassDetailed = vec3(0.0);

    // 2. Chỉ đọc Texture Cỏ (texture0) khi pixel thực sự có thành phần cỏ (mask < 0.999)
    if (mask < 0.999) {
        mediump vec3 grass = textureGrad(texture0, baseUV, baseDx, baseDy).rgb;
        grassDetailed = grass * (1.0 + 0.30 * grainOffset);

        // Chuyển sắc tự nhiên theo vùng (Broad Color & Hue Shift)
        mediump float varCentered = colorVar - 0.5;
        mediump vec3 hueTint = vec3(
            1.0 + varCentered * GRASS_LUSH_SHIFT,
            1.0 + varCentered * (GRASS_LUSH_SHIFT * 0.5),
            1.0 - varCentered * GRASS_LUSH_SHIFT
        );
        grassDetailed *= hueTint * (1.0 + 0.28 * varCentered);
    }

    // 3. Chỉ đọc Texture Đất (texture3) khi pixel thực sự có đất (mask > 0.001)
    mediump vec3 blended = grassDetailed;
    if (mask > 0.001) {
        mediump vec3 dirt = textureGrad(texture3, baseUV, baseDx, baseDy).rgb;
        dirt *= (1.0 + 0.22 * grainOffset) * (0.92 + 0.16 * colorVar);

        if (mask >= 0.999) {
            blended = dirt;
        } else {
            // Viền đất ẩm tối màu ở ranh giới Cỏ - Đất (đạt đỉnh tại mask = 0.5)
            mediump float rim = mask * (1.0 - mask) * 4.0;
            blended = mix(grassDetailed, dirt, mask);
            blended *= 1.0 - (GRASS_RIM_DARKEN * rim);
        }
    }

    blended *= colDiffuse.rgb;

    // 4. Tính toán Ánh sáng (Lighting) với chuẩn hóa vector an toàn
    vec3 normal = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 lightDir = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    // Cỏ có lá mỏng tán xạ ánh sáng (Subsurface Wrap = 0.15) trong khi Đất phẳng (Wrap = 0.0)
    mediump float wrapFactor = 0.15 * (1.0 - mask);
    mediump float diff = calcDiffuse(normal, lightDir, wrapFactor);

    mediump vec3 lit = blended * (u_ambientColor + diff * u_lightColor);
    lit += VFXLights_Accumulate(fragPosition, normal, blended);

    finalColor = vec4(lit, colDiffuse.a);
}