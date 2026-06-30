#version 330
#include "core/shaders/common/vs_header.glsl"

// u_time tự động được truyền bởi SkillManager_BeginShader
void main() {
    vec3 pos = vertexPosition;
    
    // Displacement nhỏ tạo gợn sóng lăn tăn (tránh làm phẳng mesh)
    float waveX = sin(pos.x * 0.35 + u_time * 3.5);
    float waveZ = cos(pos.z * 0.35 + u_time * 2.8);
    pos.y += waveX * waveZ * 1.8;
    
    // Xuất ra VS_FinalOutput như bắt buộc
    VS_FinalOutput(pos);
}