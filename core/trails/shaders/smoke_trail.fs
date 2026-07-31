#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    finalColor = texel * fragColor;
}
