#version 330
#include "core/shaders/common/vs_header.glsl"

// Test: noise.glsl có thể include trong VS
#include "core/shaders/common/noise.glsl"

uniform float u_phase;   // [0..1] tiến trình skill — dùng để animate spawn

void main() {
    // Nhẹ warp vertex theo FBM tạo cảm giác orb không phải hình cầu hoàn hảo
    // Test fbm2() từ noise.glsl trực tiếp trong VS
    float warp = fbm2(vertexPosition.xz * 0.08 + u_time * 0.2) - 0.5;
    vec3 displaced = vertexPosition + vertexNormal * warp * 1.8 * u_phase;

    VS_FinalOutput(displaced);
}
