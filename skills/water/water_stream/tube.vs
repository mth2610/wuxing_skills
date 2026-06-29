#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Water Stream Skill — Vertex Shader
//
// Đặc trưng của skill:
//   - Displacement "phình-thắt" mô phỏng luồng nước chạy dọc ống
//   - Hai lớp sóng sin chồng nhau: swell (dài, chậm) + bump (ngắn, nhanh)
//   - Dampen tại 2 đầu ống để tránh gờ cứng
// ============================================================

uniform float u_uvLength; // chiều dài UV thực tế dọc đường Bezier (set từ C-side)

// Hàm displacement đặc trưng của Water Stream.
// t   — vị trí dọc ống [0..1], tính từ vertexTexCoord.y / u_uvLength
// phi — góc quanh ống [0..2π], tính từ vertexTexCoord.x * 2π
float getDisplacement(float t, float phi) {
    float swell = sin(t * 4.0  - u_time * 12.0);              // phình/thắt dài, chậm
    float bump  = sin(t * 8.0  - u_time * 20.0) * cos(phi * 2.0); // múi nhỏ, trượt nhanh
    float irregularity = swell * 0.5 + bump * 0.5;
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t); // bo mềm 2 đầu
    return irregularity * dampen * 4.0;
}

void main() {
    float t   = vertexTexCoord.y / u_uvLength;
    float phi = vertexTexCoord.x * 6.28318;

    vec3 displacedPos = vertexPosition + vertexNormal * getDisplacement(t, phi);
    VS_FinalOutput(displacedPos);
}
