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

    // DỊCH CHUYỂN CĂN GIỮA quanh 0: (phase - 0.5), không phải phase.
    //
    // Đây là chỗ kỹ thuật này hỏng suốt từ đầu. Trọng số blend abs(2*p - 1)
    // giả định pha 0.5 là pha KHÔNG méo — đó là quy ước của Valve. Nếu dịch
    // chuyển tính bằng `phase` thay vì `phase - 0.5` thì lớp đang được ưu
    // tiên hoàn toàn lại chính là lớp méo NHIỀU NHẤT, còn lớp méo bằng 0
    // luôn nhận trọng số 0. Cross-fade khi đó nhấp nháy độ méo thay vì giấu
    // nó đi — tức nó làm đúng điều ngược lại với mục đích tồn tại.
    vec4 col0 = texture(texture0, uv + flow * (phase0 - 0.5) * uStrength);
    vec4 col1 = texture(texture0, uv + flow * (phase1 - 0.5) * uStrength);

    // Đạt hiệu ứng cuộn chảy năng lượng mượt mà, vô tận bằng phép nội suy pha
    finalColor = mix(col0, col1, blend) * colDiffuse * fragColor;
}