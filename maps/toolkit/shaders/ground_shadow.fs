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

// [TỐI ƯU BĂNG THÔNG & VISUAL]: Thay 4x ShadowTapBilinear (16 lần đọc texture) bằng bộ lọc
// 3x3 Quadratic B-Spline Tent PCF chỉ dùng đúng 9 lần đọc textureLod(..., 0.0) (giảm 43.75%
// băng thông lấy mẫu shadow map) đồng thời cho vùng nửa tối (penumbra) mượt liên tục bậc C1.
float ShadowFactor(vec3 worldPos) {
    vec4 posLS = u_lightVP * vec4(worldPos, 1.0);
    if (abs(posLS.w) < 1e-6) return 1.0;

    vec3 proj = (posLS.xyz / posLS.w) * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.z < 0.0 || proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0) {
        return 1.0;
    }

    float texel = max(u_shadowTexel, 1e-6);
    float res = 1.0 / texel;
    float z = proj.z - 0.0015;

    vec2 t = proj.xy * res - 0.5;
    vec2 f = fract(t);
    vec2 base = (floor(t) + 0.5) * texel;

    // Trọng số B-Spline bậc 2 mượt C1 (tổng wx0 + wx1 + wx2 == 1.0, wy0 + wy1 + wy2 == 1.0)
    vec2 omf = 1.0 - f;
    vec2 fm  = f - 0.5;
    vec2 w0  = 0.5 * omf * omf;
    vec2 w1  = 0.75 - fm * fm;
    vec2 w2  = 0.5 * f * f;

    // 9 mẫu lưới 3x3 với textureLod(..., 0.0) an toàn tuyệt đối trong nhánh động SPIR-V
    float s00 = step(z, textureLod(texture0, base + vec2(-texel, -texel), 0.0).r);
    float s10 = step(z, textureLod(texture0, base + vec2(   0.0, -texel), 0.0).r);
    float s20 = step(z, textureLod(texture0, base + vec2( texel, -texel), 0.0).r);

    float s01 = step(z, textureLod(texture0, base + vec2(-texel,    0.0), 0.0).r);
    float s11 = step(z, textureLod(texture0, base,                        0.0).r);
    float s21 = step(z, textureLod(texture0, base + vec2( texel,    0.0), 0.0).r);

    float s02 = step(z, textureLod(texture0, base + vec2(-texel,  texel), 0.0).r);
    float s12 = step(z, textureLod(texture0, base + vec2(   0.0,  texel), 0.0).r);
    float s22 = step(z, textureLod(texture0, base + vec2( texel,  texel), 0.0).r);

    float row0 = s00 * w0.x + s10 * w1.x + s20 * w2.x;
    float row1 = s01 * w0.x + s11 * w1.x + s21 * w2.x;
    float row2 = s02 * w0.x + s12 * w1.x + s22 * w2.x;
    float shadow = row0 * w0.y + row1 * w1.y + row2 * w2.y;

    // Làm mờ mềm rìa ngoài cùng của frustum bóng để tránh vệt cắt vuông góc
    vec2 borderDist = min(proj.xy, 1.0 - proj.xy);
    float borderFade = smoothstep(0.0, 0.03, min(borderDist.x, borderDist.y));
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
    float shadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        shadow = ShadowFactor(fragWorldPos);
    }
    vec3 floorLit = fragColor.rgb * mix(0.30, 1.0, shadow);
    floorLit += VFXLights_AccumulateFlat(fragWorldPos, fragColor.rgb);
    finalColor = vec4(floorLit, fragColor.a);
}
