#version 330

#include "core/shaders/common/vfx_lights.glsl"

in vec4 fragColor;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform mat4      u_lightVP;
uniform float     u_shadowEnabled;
uniform float     u_shadowTexel;

out vec4 finalColor;

#define GROUND_SHADOW_PCF_RADIUS 3
#define GROUND_SHADOW_PCF_STEP   1.0

// Giữ hàm helper tương thích nhưng dùng textureLod(..., 0.0) để an toàn trong rẽ nhánh Vulkan
float ShadowTapBilinear(vec2 uv, float z, float res) {
    vec2  t    = uv * res - 0.5;
    vec2  f    = fract(t);
    vec2  base = (floor(t) + 0.5) * u_shadowTexel;
    float s00 = step(z, textureLod(texture0, base, 0.0).r);
    float s10 = step(z, textureLod(texture0, base + vec2(u_shadowTexel, 0.0), 0.0).r);
    float s01 = step(z, textureLod(texture0, base + vec2(0.0, u_shadowTexel), 0.0).r);
    float s11 = step(z, textureLod(texture0, base + vec2(u_shadowTexel, u_shadowTexel), 0.0).r);
    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

// [TỐI ƯU BĂNG THÔNG 2 GIAI ĐOẠN & VISUAL]:
// - Giai đoạn 1: Kiểm tra biên frustum và lấy mẫu 5 điểm chữ thập (Cross Early-Out). Nếu cả 5 điểm
//   đều sáng hoặc đều tối (chiếm >85% diện tích mặt đất), thoát sớm ngay chỉ với 5 lần đọc texture!
// - Giai đoạn 2: Chỉ tại rìa bóng bán dạ (penumbra) mới đọc tiếp 4 góc còn lại để hoàn thiện bộ lọc
//   3x3 Quadratic B-Spline Tent PCF mượt liên tục bậc C1.
float ShadowFactor(vec3 worldPos) {
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    if (posLS.w <= 1e-6) return 1.0;

    vec3 proj = (posLS.xyz / posLS.w) * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0 || proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0) {
        return 1.0;
    }

    vec2 borderDist = min(proj.xy, 1.0 - proj.xy);
    float borderFade = smoothstep(0.0, 0.03, min(borderDist.x, borderDist.y));
    if (borderFade <= 0.0) return 1.0;

    float texel = max(u_shadowTexel, 1e-6);
    float res = 1.0 / texel;
    float z = proj.z - 0.0015;

    vec2 t = proj.xy * res - 0.5;
    vec2 baseFloor = floor(t);
    vec2 f = t - baseFloor;
    vec2 base = (baseFloor + 0.5) * texel;

    // Giai đoạn 1: 5 mẫu chữ thập tâm
    float s10 = step(z, textureLod(texture0, base + vec2(   0.0, -texel), 0.0).r);
    float s01 = step(z, textureLod(texture0, base + vec2(-texel,    0.0), 0.0).r);
    float s11 = step(z, textureLod(texture0, base,                        0.0).r);
    float s21 = step(z, textureLod(texture0, base + vec2( texel,    0.0), 0.0).r);
    float s12 = step(z, textureLod(texture0, base + vec2(   0.0,  texel), 0.0).r);

    float crossSum = s10 + s01 + s11 + s21 + s12;
    if (crossSum >= 4.999) return 1.0;
    if (crossSum <= 0.001) return 1.0 - borderFade;

    // Giai đoạn 2: 4 mẫu góc hoàn thiện lưới 3x3 B-Spline bậc 2
    float s00 = step(z, textureLod(texture0, base + vec2(-texel, -texel), 0.0).r);
    float s20 = step(z, textureLod(texture0, base + vec2( texel, -texel), 0.0).r);
    float s02 = step(z, textureLod(texture0, base + vec2(-texel,  texel), 0.0).r);
    float s22 = step(z, textureLod(texture0, base + vec2( texel,  texel), 0.0).r);

    vec2 omf = 1.0 - f;
    vec2 fm  = f - 0.5;
    vec2 w0  = 0.5 * omf * omf;
    vec2 w1  = 0.75 - fm * fm;
    vec2 w2  = 0.5 * f * f;

    float row0 = s00 * w0.x + s10 * w1.x + s20 * w2.x;
    float row1 = s01 * w0.x + s11 * w1.x + s21 * w2.x;
    float row2 = s02 * w0.x + s12 * w1.x + s22 * w2.x;
    float shadow = row0 * w0.y + row1 * w1.y + row2 * w2.y;

    return mix(1.0, shadow, borderFade);
}

#define GROUND_SHADOW_DEBUG_PROJ 0

void main() {
#if GROUND_SHADOW_DEBUG_PROJ
    if (u_shadowEnabled > 0.5) {
        vec4 p = u_lightVP * vec4(fragWorldPos, 1.0);
        vec3 pr = p.xyz / p.w * 0.5 + 0.5;
        if (pr.x < 0.0 || pr.x > 1.0 || pr.y < 0.0 || pr.y > 1.0) {
            finalColor = vec4(0.0, 0.0, 0.4, 1.0);
            return;
        }
        float d = textureLod(texture0, pr.xy, 0.0).r;
        finalColor = vec4(vec3(d), 1.0);
        return;
    }
#endif
    float shadow = (u_shadowEnabled > 0.5) ? ShadowFactor(fragWorldPos) : 1.0;
    vec3 floorLit = fragColor.rgb * mix(0.30, 1.0, shadow);
    floorLit += VFXLights_AccumulateFlat(fragWorldPos, fragColor.rgb);
    finalColor = vec4(floorLit, fragColor.a);
}