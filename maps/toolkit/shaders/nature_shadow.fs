#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    if (u_useTexture != 0 && fragTexCoord.x >= 0.0 &&
        texture(texture0, fragTexCoord).a < u_alphaCutoff)
        discard;
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
