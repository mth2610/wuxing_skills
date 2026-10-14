#version 330

#include "core/shaders/common/vfx_lights.glsl"

in vec4 fragColor;
in vec3 fragWorldPos;

uniform sampler2D texture0; // shadow depth map
uniform mat4      u_lightVP;
uniform float     u_shadowEnabled;
uniform float     u_shadowTexel; // 1.0 / resolution

out vec4 finalColor;

// ----------------------------------------------------------------------------
// 9-Tap Bilinear / Tent PCF (Thay thế cho 16-tap cũ)
// * Chỉ tốn 9 lần đọc texture thay vì 16 lần (tiết kiệm 44% băng thông).
// * Lấy mẫu lưới 3x3 texel liền kề và pha trộn mượt theo sub-texel (f.x, f.y),
//   loại bỏ hoàn toàn hiện tượng ô vuông (blocky edge) trên Point-sampled R32F.
// ----------------------------------------------------------------------------
float ShadowFactor(vec3 worldPos) {
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0) {
        return 1.0;
    }

    float z = proj.z - 0.0015; // Depth bias

    float res  = 1.0 / max(u_shadowTexel, 1e-6);
    vec2  t    = proj.xy * res - 0.5;
    vec2  f    = fract(t);
    vec2  base = (floor(t) + 0.5) * u_shadowTexel;

    float tx = u_shadowTexel;

    // 9 lượt đọc texture liền kề (3x3)
    float s00 = step(z, texture(texture0, base + vec2(-tx, -tx)).r);
    float s10 = step(z, texture(texture0, base + vec2(0.0, -tx)).r);
    float s20 = step(z, texture(texture0, base + vec2( tx, -tx)).r);

    float s01 = step(z, texture(texture0, base + vec2(-tx, 0.0)).r);
    float s11 = step(z, texture(texture0, base                ).r);
    float s21 = step(z, texture(texture0, base + vec2( tx, 0.0)).r);

    float s02 = step(z, texture(texture0, base + vec2(-tx,  tx)).r);
    float s12 = step(z, texture(texture0, base + vec2(0.0,  tx)).r);
    float s22 = step(z, texture(texture0, base + vec2( tx,  tx)).r);

    // Trọng số nội suy Bilinear/Tent liên tục (Tổng trọng số = 4.0)
    vec3 wx = vec3(1.0 - f.x, 1.0, f.x);
    vec3 wy = vec3(1.0 - f.y, 1.0, f.y);

    float row0 = dot(vec3(s00, s10, s20), wx);
    float row1 = dot(vec3(s01, s11, s21), wx);
    float row2 = dot(vec3(s02, s12, s22), wx);

    float shadow = dot(vec3(row0, row1, row2), wy) * 0.25;

    // Làm mờ mềm 4% ở mép ngoài của Shadow Map để bóng không bị cắt cụt đột ngột
    vec2 edgeDist = min(proj.xy, 1.0 - proj.xy);
    float frustumFade = smoothstep(0.0, 0.04, min(edgeDist.x, edgeDist.y));

    return mix(1.0, shadow, frustumFade);
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
        float d = texture(texture0, pr.xy).r;
        finalColor = vec4(vec3(d), 1.0);
        return;
    }
#endif

    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }

    // Giữ nguyên chuẩn màu vertex (xanh dương của Arena) và chỉ làm tối nhẹ vùng có bóng đổ
    vec3 floorLit = fragColor.rgb * mix(0.30, 1.0, shadow);
    floorLit += VFXLights_AccumulateFlat(fragWorldPos, fragColor.rgb);

    finalColor = vec4(floorLit, fragColor.a);
}