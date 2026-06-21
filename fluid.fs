#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float u_time; 

out vec4 finalColor;

void main() {
    float density = texture(texture0, fragTexCoord).a;
    
    // Ngưỡng siêu thấp để giữ lại viền glow mềm mại nhất có thể
    if (density < 0.02) {
        discard;
    }
    
    // Bảng màu năng lượng thủy
    vec3 colorOuter = vec3(0.0, 0.35, 0.9); // Xanh biển sâu (Viền)
    vec3 colorInner = vec3(0.4, 0.85, 1.0); // Xanh lơ sáng (Lõi giữa)
    vec3 colorCore  = vec3(1.0, 1.0, 1.0);  // Trắng tinh khiết (Tâm bạo kích)

    vec3 mixedColor;
    
    // Gradient hòa trộn sắc nét
    if (density < 0.4) {
        float t = smoothstep(0.02, 0.4, density);
        mixedColor = mix(colorOuter, colorInner, t);
    } else {
        float t = smoothstep(0.4, 0.8, density);
        mixedColor = mix(colorInner, colorCore, t);
    }
    
    // Caustics: Hiệu ứng ánh sáng khúc xạ trôi cuồn cuộn
    vec2 flow = vec2(u_time * 8.0, u_time * -5.0);
    vec2 pixelCoords = fragTexCoord * textureSize(texture0, 0);
    float sparkle = sin((pixelCoords.x + flow.x) * 0.2) * cos((pixelCoords.y + flow.y) * 0.2);
    sparkle = pow(clamp(sparkle, 0.0, 1.0), 3.0);
    
    // Chỉ lấp lánh ở những vùng nước đủ dày
    if (density > 0.4) {
        mixedColor += vec3(sparkle * 0.6); 
    }
    
    // Khóa Alpha mềm
    float alpha = smoothstep(0.02, 0.25, density);
    finalColor = vec4(mixedColor, alpha);
}