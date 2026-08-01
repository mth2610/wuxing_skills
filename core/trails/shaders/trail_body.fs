#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
    vec4 sheet = texture(texture0, fragTexCoord);
    float alpha = sheet.a * fragColor.a * colDiffuse.a;
    if (alpha < 0.003) discard;
    // Keep the authored sheet exactly: its RGB carries flow, filament and
    // colour variation. This is only an HDR lift for the alpha body so the
    // same textured core remains vivid against a bright scene.
    vec3 colour = sheet.rgb * fragColor.rgb * colDiffuse.rgb * 1.75;
    finalColor = vec4(colour, alpha);
}
