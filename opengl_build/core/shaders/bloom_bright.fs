#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_threshold;

out vec4 finalColor;

void main() {
    vec4 col = texture(texture0, fragTexCoord);
    
    // 1. Tính toán độ sáng dựa trên kênh màu lớn nhất (max-channel) để các màu bão hòa (lam, tím) cũng bloom rực rỡ
    float brightness = max(col.r, max(col.g, col.b));
    
    // 2. Thuật toán chống ảo ảnh cho vật thể mảnh (Knee Anti-Aliasing)
    // Tạo một vùng đệm mượt mờ quanh ngưỡng threshold để triệt tiêu răng cưa
    float knee = 0.15; 
    float soft = brightness - u_threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    
    // Cường độ ánh sáng được làm mượt bằng hàm bậc hai
    float weight = max(soft, brightness - u_threshold);
    weight /= max(brightness, 0.00001);
    
    // 3. Chặn triệt để hiện tượng "Lóa điểm" (Fireflies) khi áp sát camera
    // Giới hạn không cho năng lượng pixel tăng đột biến quá mức kiểm soát của bộ Blur
    // Boost bloom extraction weight slightly to offset clamping dilution
    vec3 brightColor = col.rgb * weight * 2.2;
    float maxEnergy = 4.0; // Ngưỡng năng lượng tối đa của một pixel bloom
    float currentEnergy = length(brightColor);
    if (currentEnergy > maxEnergy) {
        brightColor = (brightColor / currentEnergy) * maxEnergy;
    }
    
    finalColor = vec4(brightColor, col.a);
}