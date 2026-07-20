#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"

uniform vec3  u_center;
uniform float u_baseY;
uniform float u_height;
uniform float u_scrollSpeed;
uniform float u_ridgeFreq;
uniform float u_seed;
uniform float u_alphaFade;   // 1.0 bình thường, giảm dần về 0 khi grow-in / fade-out

// Cấu hình Turbulence chuẩn từ Shadertoy
#define FIRE_NUM 8.0
#define FIRE_AMP 0.4
#define FIRE_SPEED 6.0
#define FIRE_FREQ 12.0
#define FIRE_EXP 1.2

// BẮT BUỘC: Giữ nguyên vẹn hàm vnoise3 của bạn để Vulkan hoạt động ổn định
float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i);
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// BẮT BUỘC: Giữ nguyên vẹn hàm fbm3 của bạn
float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 3; k++) {
        v += a * vnoise3(p);
        p  = p * 2.1 + vec3(7.3, 13.1, 5.7);
        a *= 0.5;
    }
    return v;
}

// Thuật toán Cuộn Turbulence của @XorDev điều chỉnh nhẹ để xuất ma trận xoay 3D[cite: 1]
vec3 applyTurbulence3D(vec3 p, float F, float N, float S, float A, float E) {
    float freq = F;
    // Ma trận xoay 3D cố định xung quanh trục Y để trộn lẫn các tầng nhiễu Octave
    mat3 rot = mat3(
        0.6,  0.0,  0.8,
        0.0,  1.0,  0.0,
       -0.8,  0.0,  0.6
    );
    
    for(float i = 0.0; i < N; i++) {
        // Cuộn dọc theo trục Y kết hợp thời gian u_time
        float phase = freq * (p * rot).y + S * u_time + i;
        // Biến dạng tịnh tiến vị trí dựa trên sóng sin vuông góc[cite: 1]
        p += A * rot[0] * sin(phase) / freq;
        
        rot *= mat3(0.6, 0.0, -0.8, 0.0, 1.0, 0.0, 0.8, 0.0, 0.6);
        freq *= E;
    }
    return p;
}

void main()
{
    // Tính toán tỷ lệ chiều cao chuẩn hóa (0.0 ở đáy -> 1.0 ở đỉnh phễu)[cite: 2]
    float h = clamp((fragPosition.y - u_baseY) / max(u_height, 0.0001), 0.0, 1.0);

    // Tính toán góc xoay xung quanh tâm phễu cho Mesh không có UV[cite: 2]
    vec2 toCenter = fragPosition.xz - u_center.xz;
    float angle = atan(toCenter.y, toCenter.x);

    // 1. Chuyển đổi hệ tọa độ (Góc, Chiều cao) thành Không gian 3D hình trụ tròn ổn định
    // Điều này giúp map dữ liệu mượt mà vào hàm fbm3(vec3) mà không lo bị lỗi kéo giãn bề mặt
    float xstretch = 2.0 - 1.5 * smoothstep(0.0, 1.0, h);
    float u = angle * u_ridgeFreq * 0.5 * xstretch;
    float v = h * 4.0 - u_time * u_scrollSpeed;

    // Biến đổi tọa độ dòng chảy 2D thành một không gian 3D cuốn quanh trục
    vec3 localSpace3D = vec3(cos(u), v, sin(u)) + vec3(u_seed);

    // 2. Ép thuật toán cuộn dòng chảy Turbulence vào không gian 3D này[cite: 1]
    localSpace3D = applyTurbulence3D(localSpace3D, FIRE_FREQ, FIRE_NUM, FIRE_SPEED, FIRE_AMP, FIRE_EXP);

    // 3. Sử dụng trực tiếp hàm fbm3 "Vulkan-safe" của bạn để lấy vân lửa tầng[cite: 2]
    float body  = fbm3(localSpace3D);
    float licks = fbm3(localSpace3D * 2.3 - vec3(0.0, u_time * u_scrollSpeed * 1.6, 0.0) + vec3(u_seed * 1.7));
    float grain = body * 0.65 + licks * 0.35;

    // 4. Pha trộn dải màu Gradient dựa trên chiều cao h[cite: 2]
    vec3 core = vec3(1.0, 0.95, 0.75);
    vec3 mid  = vec3(1.0, 0.55, 0.12);
    vec3 tip  = vec3(0.55, 0.08, 0.03);
    
    vec3 col = mix(core, mid, smoothstep(0.0, 0.45, h));
    col = mix(col, tip, smoothstep(0.45, 1.0, h));
    col = mix(col, col * 1.8, grain);

    // Tích hợp hiệu ứng ánh sáng rìa Fresnel (Rim Light)[cite: 2]
    vec3 viewDir = normalize(viewPos - fragPosition);
    float rim = calcFresnel(fragNormal, viewDir, 2.5);
    col += vec3(1.0, 0.6, 0.2) * rim * 0.6;

    // 5. Tính toán mép lởm chởm ở ngọn phễu bằng fbm3 đã qua Turbulence[cite: 1, 2]
    float edgeNoise = fbm3(localSpace3D * 1.3 + vec3(u_seed * 2.0));
    float tipFade = smoothstep(1.0, 0.55, h + (edgeNoise - 0.5) * 0.35);
    
    // Alpha tổng hợp bảo lưu cơ chế fade của kỹ năng[cite: 2]
    float alpha = tipFade * mix(0.55, 1.0, grain) * u_alphaFade;

    // Tối ưu hóa discard pixel[cite: 2]
    if (alpha < 0.02) discard;

    // Áp dụng Exponential Tonemap từ Shadertoy bảo vệ dải màu không bị cháy sáng[cite: 1]
    col = 1.0 - exp(-col);

    finalColor = vec4(col, alpha);
}