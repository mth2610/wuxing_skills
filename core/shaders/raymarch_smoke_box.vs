#version 330
#include "core/shaders/common/vs_header.glsl"

void main() {
    // VS_FinalOutput là hàm bắt buộc của engine để xuất tọa độ ra màn hình
    // và tính toán Varyings (fragPosition, fragNormal, fragTexCoord) truyền sang FS
    VS_FinalOutput(vertexPosition);
}