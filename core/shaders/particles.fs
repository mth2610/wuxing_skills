#version 430

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;
uniform sampler2D texture0;

void main() {
    vec4 texelColor = texture(texture0, fragTexCoord);
    finalColor = texelColor * fragColor;
    
    // Cắt rìa trong suốt để tối ưu Fillrate
    if (finalColor.a < 0.01) discard; 
}