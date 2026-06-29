#version 330 core
// Fragment shader — dùng chung cho cả COMPUTE path và CPU/VBO path

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    finalColor  = texel * fragColor;

    // Discard pixel trong suốt hoàn toàn để tối ưu fillrate
    if (finalColor.a < 0.01) discard;
}
