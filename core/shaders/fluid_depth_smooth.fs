#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 u_texel;
void main() {
    float d = 1.0;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
        d = min(d, texture(texture0, fragTexCoord + vec2(x, y)*u_texel).r);
    finalColor = vec4(d, 0.0, 0.0, 1.0);
}
