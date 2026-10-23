#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;
uniform float u_scatter;

out vec4 finalColor;

void main() {
    vec4 d = u_texelSize.xyxy * vec4(-0.5, 0.5, 0.5, -0.5);

    // 4 bilinear taps evaluate the exact 3x3 tent filter.
    // Nhân trực tiếp với 0.25 thay vì (* 4.0) rồi (/ 16.0).
    vec3 tent = (texture(texture0, fragTexCoord + d.xy).rgb
               + texture(texture0, fragTexCoord + d.zy).rgb
               + texture(texture0, fragTexCoord + d.xw).rgb
               + texture(texture0, fragTexCoord + d.zw).rgb) * 0.25;

    finalColor = vec4(tent, clamp(u_scatter, 0.0, 1.0));
}