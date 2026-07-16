#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 u_direction; // Vector hướng mờ (ví dụ: (1/w, 0) cho H-blur, (0, 1/h) cho V-blur)

out vec4 finalColor;

// Hàm kiểm tra xem tọa độ UV có nằm trong bộ đệm màn hình không (Tránh ảo ảnh rìa)
float checkBounds(vec2 uv) {
    return (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) ? 1.0 : 0.0;
}

void main() {
    vec2 tc = fragTexCoord;
    
    // Trọng số phân bố Gaussian 9 lớp chuẩn (Tổng các hệ số = 1.0)
    // Tâm (0)
    vec4 col = texture(texture0, tc) * 0.2270270270;
    
    // Bước nhảy 1
    vec2 offset1 = u_direction * 1.0;
    col += texture(texture0, tc + offset1) * 0.1945945946 * checkBounds(tc + offset1);
    col += texture(texture0, tc - offset1) * 0.1945945946 * checkBounds(tc - offset1);
    
    // Bước nhảy 2
    vec2 offset2 = u_direction * 2.0;
    col += texture(texture0, tc + offset2) * 0.1216216216 * checkBounds(tc + offset2);
    col += texture(texture0, tc - offset2) * 0.1216216216 * checkBounds(tc - offset2);
    
    // Bước nhảy 3
    vec2 offset3 = u_direction * 3.0;
    col += texture(texture0, tc + offset3) * 0.0540540541 * checkBounds(tc + offset3);
    col += texture(texture0, tc - offset3) * 0.0540540541 * checkBounds(tc - offset3);
    
    // Bước nhảy 4
    vec2 offset4 = u_direction * 4.0;
    col += texture(texture0, tc + offset4) * 0.0162162162 * checkBounds(tc + offset4);
    col += texture(texture0, tc - offset4) * 0.0162162162 * checkBounds(tc - offset4);
    
    finalColor = col;
}