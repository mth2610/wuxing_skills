#version 330
#include "core/shaders/common/vs_header.glsl"

// Hàm đẩy vertex tạo hiệu ứng nước núng nính
float getJiggleWobble(vec3 pos) {
    // Tần số dao động thấp (0.3) nhân với vận tốc u_time lớn (15.0) tạo độ "sóng sánh"
    float swell = sin(pos.y * 0.3 + u_time * 15.0);
    float bump  = cos(pos.x * 0.3 + u_time * 10.0) * sin(pos.z * 0.3 - u_time * 12.0);
    
    // Khuyếch đại biên độ (amplitude) lên 3.0 units để thấy rõ sự biến dạng ở scale bự
    return (swell * 0.6 + bump * 0.4) * 3.0; 
}

void main() {
    // vertexPosition bản chất đã nằm ở world-space khi dùng DrawCoreSphere
    float wobble = getJiggleWobble(vertexPosition);
    vec3 displacedPos = vertexPosition + vertexNormal * wobble;
    
    // Bắt buộc tuân thủ API
    VS_FinalOutput(displacedPos);
}