#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec2 u_texelSize;

out vec4 finalColor;

void main() {
    /* Four bilinear half-texel taps are exactly a separable 3x3 tent:
     * (1 2 1 / 2 4 2 / 1 2 1) / 16. Run at raymarch resolution so the
     * one-pixel phase lattice is removed before the 3x/4x display upscale. */
    vec4 sum = texture(texture0, fragTexCoord +
                       vec2(-0.5, -0.5) * u_texelSize)
             + texture(texture0, fragTexCoord +
                       vec2( 0.5, -0.5) * u_texelSize)
             + texture(texture0, fragTexCoord +
                       vec2(-0.5,  0.5) * u_texelSize)
             + texture(texture0, fragTexCoord +
                       vec2( 0.5,  0.5) * u_texelSize);
    finalColor = sum * 0.25 * fragColor;
}
