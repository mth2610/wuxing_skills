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
    vec2 uv = fragTexCoord;
    vec2 t = u_texelSize;

    // 9-tap Jimenez 3x3 tent filter (trọng số: tâm 4/16, cạnh 2/16, góc 1/16).
    // Mở rộng bán kính lên 1.0 * u_texelSize giúp xóa bỏ hoàn toàn vân lưới vuông
    // khi phóng đại từ các tầng mip thấp (1/64 -> 1/32 -> 1/16 -> 1/8 -> 1/4).
    vec3 c  = texture(texture0, uv).rgb * 0.25;

    vec3 n0 = texture(texture0, uv + vec2(-t.x,  0.0)).rgb;
    vec3 n1 = texture(texture0, uv + vec2( t.x,  0.0)).rgb;
    vec3 n2 = texture(texture0, uv + vec2( 0.0, -t.y)).rgb;
    vec3 n3 = texture(texture0, uv + vec2( 0.0,  t.y)).rgb;

    vec3 d0 = texture(texture0, uv + vec2(-t.x, -t.y)).rgb;
    vec3 d1 = texture(texture0, uv + vec2( t.x, -t.y)).rgb;
    vec3 d2 = texture(texture0, uv + vec2(-t.x,  t.y)).rgb;
    vec3 d3 = texture(texture0, uv + vec2( t.x,  t.y)).rgb;

    vec3 tent = c + (n0 + n1 + n2 + n3) * 0.125 + (d0 + d1 + d2 + d3) * 0.0625;

    finalColor = vec4(max(tent, vec3(0.0)), clamp(u_scatter, 0.0, 1.0));
}