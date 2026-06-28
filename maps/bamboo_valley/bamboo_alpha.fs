#version 330

// Dữ liệu nội suy từ Vertex Shader của Raylib
in vec2 fragTexCoord;
in vec4 fragColor;

// Các biến Uniform mặc định của Raylib
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Màu kết xuất cuối cùng
out vec4 finalColor;

void main() {
    // Lấy màu từ texture của lá tre
    vec4 texelColor = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    
    // KHỬ VIỀN ĐEN: Nếu độ trong suốt (alpha) bé hơn 0.25 (vùng trong suốt của lá),
    // ta lập tức hủy vẽ pixel đó (discard) để tránh ghi đè Z-Buffer tạo viền quads đen.
    if (texelColor.a < 0.25) discard;
    
    finalColor = texelColor;
}
