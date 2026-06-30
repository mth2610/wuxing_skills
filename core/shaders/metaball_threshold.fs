#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0; // blurred blob density mask (.r = density)
uniform vec4 u_tint;
uniform float u_threshold;  // ngưỡng bắt đầu hiện hình [0..1]
uniform float u_smoothness; // độ mượt viền quanh ngưỡng

void main() {
    float density = texture(texture0, fragTexCoord).r;
    float alpha = smoothstep(u_threshold - u_smoothness, u_threshold + u_smoothness, density);

    // Viền sáng mỏng quanh mép khối lỏng (rim light) — cảm giác bóng bẩy hơn
    // 1 khối phẳng màu đặc.
    float rim = smoothstep(u_threshold - u_smoothness, u_threshold, density) -
               smoothstep(u_threshold, u_threshold + u_smoothness, density);

    vec3 color = u_tint.rgb + rim * 0.6;
    finalColor = vec4(color, alpha * u_tint.a);
}
