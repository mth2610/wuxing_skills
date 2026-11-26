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

    // 4-tap bilinear tent filter (±0.5 texel offsets) — bit-equivalent to 9-tap 3x3 tent
    // (center 4/16, edges 2/16, corners 1/16) with linear filtering while cutting texture fetches >50%.
    vec2 h = t * 0.5;
    vec3 tent = (texture(texture0, uv + vec2(-h.x, -h.y)).rgb +
                 texture(texture0, uv + vec2( h.x, -h.y)).rgb +
                 texture(texture0, uv + vec2(-h.x,  h.y)).rgb +
                 texture(texture0, uv + vec2( h.x,  h.y)).rgb) * 0.25;

    finalColor = vec4(max(tent, vec3(0.0)), clamp(u_scatter, 0.0, 1.0));
}