#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    // 4-tap box at ±1 texel corners; bilinear HW gives free 4-tap each sample.
    // Produces a wide, alias-free downsample pass for the dual-filter pyramid.
    vec4 sum = texture(texture0, uv + vec2(-1.0, -1.0) * u_texelSize)
             + texture(texture0, uv + vec2( 1.0, -1.0) * u_texelSize)
             + texture(texture0, uv + vec2(-1.0,  1.0) * u_texelSize)
             + texture(texture0, uv + vec2( 1.0,  1.0) * u_texelSize);
    finalColor = sum * 0.25;
}
