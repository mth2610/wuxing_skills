#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // Main Texture (Vị trí Slot 0 mặc định)
uniform sampler2D flowTex;    // Flow Map (Vị trí Slot 1 được bind từ C)
uniform vec4      colDiffuse;
uniform float     uTime;
uniform float     uSpeed;
uniform float     uStrength;
uniform float     uTiling;

out vec4 finalColor;

void main() {
    // Áp dụng Tiling lên tọa độ UV chính
    vec2 uv = fragTexCoord * uTiling;

    // Giải mã Vector dòng chảy từ dải màu [0, 1] về dải vector hướng [-1, 1]
    vec2 flow = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;

    // Kỹ thuật Triệt tiêu Vết nứt dòng chảy bằng cơ chế 2 Pha lệch nhau 0.5 chu kỳ tuần hoàn
    float phase0 = fract(uTime * uSpeed);
    float phase1 = fract(uTime * uSpeed + 0.5);

    // Tính toán trọng số Blend dạng sóng tam giác (Đỉnh cao nhất tại 0.5)
    float blend = abs(phase0 * 2.0 - 1.0);

    // Lấy mẫu kết cấu bề mặt dịch chuyển qua 2 pha độc lập
    vec4 col0 = texture(texture0, uv + flow * phase0 * uStrength);
    vec4 col1 = texture(texture0, uv + flow * phase1 * uStrength);

    // Đạt hiệu ứng cuộn chảy năng lượng mượt mà, vô tận bằng phép nội suy pha
    finalColor = mix(col0, col1, blend) * colDiffuse * fragColor;
}