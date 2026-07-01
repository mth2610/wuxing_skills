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
    // Tent filter: 4 bilinear samples in + cross pattern.
    // Upsampling from a smaller mip; bilinear hardware widens the footprint further.
    vec4 sum = texture(texture0, uv + vec2(-1.0,  0.0) * u_texelSize)
             + texture(texture0, uv + vec2( 1.0,  0.0) * u_texelSize)
             + texture(texture0, uv + vec2( 0.0, -1.0) * u_texelSize)
             + texture(texture0, uv + vec2( 0.0,  1.0) * u_texelSize);
    finalColor = sum * 0.25;
}
