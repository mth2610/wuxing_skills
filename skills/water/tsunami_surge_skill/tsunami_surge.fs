#version 330

#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

uniform float u_dissolve;

void main() {
    float n = hash3(floor(fragPosition * 0.15));
    float edgeFactor;
    
    if (dissolveCalc(n, u_dissolve, 0.12, edgeFactor) >= 1.0) discard;

    // 1. Màu sắc nước biển sâu (Deep Water) và bọt sủi trắng (Crest)
    vec3 deepWaterColor = vec3(0.02, 0.2, 0.45);
    vec3 crestFoamColor = vec3(0.7, 0.95, 1.0);

    // Sử dụng fbm trượt UV để tạo cảm giác bọt nước cuộn chảy xiết dọc thân sóng
    float flow = fbm2(fragTexCoord * 12.0 - vec2(0.0, u_time * 5.0));
    vec3 baseColor = mix(deepWaterColor, crestFoamColor, flow * 0.6);

    // 2. Chiếu sáng 3D Isometric chuẩn
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float diffuse = calcDiffuse(fragNormal, lightDir, 0.25);
    float fresnel = calcFresnel(fragNormal, viewDir, 3.0);

    vec3 finalLight = baseColor * diffuse;
    
    // Vùng viền bắt ánh sáng tạo cảm giác sương nước ma mị
    finalLight += crestFoamColor * smoothstep(0.4, 1.0, fresnel) * 1.5;

    // Khi ngọn sóng tan rã, phần rìa biến thành bọt nước trắng xóa bắn tung tóe
    finalLight = mix(finalLight, vec3(1.0, 1.0, 1.0), edgeFactor * 2.5);

    // Áp dụng Alpha hòa trộn để làm khối nước trong suốt
    finalColor = vec4(finalLight, 0.85 - (u_dissolve * 0.85));
}