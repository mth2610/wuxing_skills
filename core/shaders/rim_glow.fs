#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Các thông số Uniform mở rộng
uniform float rimPower = 2.5;
uniform float rimBrightness = 2.0;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Dựa vào tọa độ trục X (0=Trái, 1=Phải) trên dải dây ruy-băng để bóc tách biên tỏa sáng (Rim Effect)
    float centerDist = abs(fragTexCoord.x * 2.0 - 1.0);
    float rim = pow(1.0 - centerDist, rimPower);
    
    vec4 baseColor = texColor * colDiffuse * fragColor;
    
    // Hòa trộn tăng sáng lõi năng lượng ở giữa kết hợp với hào quang phát sáng dọc 2 biên dải sét
    finalColor = vec4(baseColor.rgb * (1.0 + rim * rimBrightness), baseColor.a);
}