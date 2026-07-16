#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D noiseTex; // Bản đồ nhiễu phân rã ranh giới

uniform float dissolveAmount; // Tiến trình phân rã [0.0 .. 1.0]
uniform float edgeWidth = 0.04;
uniform vec4 edgeColor = vec4(0.3, 0.7, 1.0, 1.0); // Màu lửa điện bao quanh lát cắt rã

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    float noise = texture(noiseTex, fragTexCoord).r;
    
    // Loại bỏ điểm pixel hoàn toàn nếu giá trị noise nhỏ hơn ngưỡng hiện tại
    if (noise < dissolveAmount) {
        discard;
    }
    
    vec4 finalTex = texColor * colDiffuse * fragColor;
    
    // Xử lý tạo dải biên rực sáng bao quanh lát cắt tan biến (Edge Glow)
    if (noise < dissolveAmount + edgeWidth) {
        float edgeLerp = (dissolveAmount + edgeWidth - noise) / edgeWidth;
        finalTex.rgb = mix(finalTex.rgb, edgeColor.rgb * 3.0, edgeLerp);
    }
    
    finalColor = finalTex;
}