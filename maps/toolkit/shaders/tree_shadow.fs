#version 330

in vec2 fragTexCoord;
uniform sampler2D u_treeFoliageAtlas;
out vec4 finalColor;

void main()
{
    if (fragTexCoord.x >= 0.0) {
        float a = texture(u_treeFoliageAtlas, fragTexCoord).a;
        if (a < 0.42)
            discard;
    }
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
