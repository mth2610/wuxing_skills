#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Tính toán hàm Falloff hình tròn mềm từ tâm UV (0.5, 0.5)
    float dist = length(fragTexCoord - vec2(0.5));
    float softMask = clamp(1.0 - dist * 2.0, 0.0, 1.0);
    softMask = softMask * softMask; // Bình phương để làm mịn rìa mờ biên
    
    float alpha = texColor.a * colDiffuse.a * fragColor.a * softMask;
    
    // Đột phá cường độ phát sáng (Boost HDR Glow) bằng cách nhân tích hợp màu khuếch tán
    vec3 rgb = texColor.rgb * colDiffuse.rgb * fragColor.rgb * 1.5;
    
    finalColor = vec4(rgb, alpha);
}