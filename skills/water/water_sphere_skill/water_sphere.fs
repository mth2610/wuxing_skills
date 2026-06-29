#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

uniform float u_dissolve;

// Hàm nhiễu bề mặt (tương tự độ nhám của nước) để làm rối Fresnel
float getSurfaceRipple(vec2 uv) {
    return sin(uv.y * 15.0 - u_time * 8.0) * cos(uv.x * 15.0 + u_time * 6.0) * 1.5;
}

void main() {
    // 1. Tính toán normal perturbation để bề mặt nước sần sùi bắt sáng tốt hơn
    const float eps = 0.02;
    vec2 hDelta = vec2(
        getSurfaceRipple(fragTexCoord - vec2(eps, 0.0 )) - getSurfaceRipple(fragTexCoord + vec2(eps, 0.0 )),
        getSurfaceRipple(fragTexCoord - vec2(0.0,  eps)) - getSurfaceRipple(fragTexCoord + vec2(0.0,  eps))
    );
    vec3 normal = perturbNormal(fragNormal, hDelta, 0.4);

    // 2. Vector ánh sáng (sử dụng hướng đèn cố định của Isometric Night-time Arena)
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
    
    // 3. Tính Fresnel và Specular (Độ bóng bẩy của nước)
    float fresnel  = calcFresnel(normal, viewDir, 2.5);
    float specular = calcSpecular(normal, lightDir, viewDir, 256.0) * 6.0;

    // 4. Bảng màu Water Sphere (Sâu thẳm ở giữa, viền sáng)
    vec3 coreColor = vec3(0.01, 0.20, 0.50); // Xanh đậm
    vec3 edgeColor = vec3(0.20, 0.65, 1.00); // Xanh lơ
    vec3 baseColor = mix(coreColor, edgeColor, pow(fresnel, 1.5));

    // Thêm phát sáng rim
    baseColor += vec3(0.5, 0.85, 1.0) * fresnel * 0.4;

    // 5. Tính Alpha trong suốt
    float alpha = mix(0.4, 0.95, fresnel) * (1.0 - u_dissolve);

    finalColor = vec4(baseColor + vec3(specular), alpha);
}