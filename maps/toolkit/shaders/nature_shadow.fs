#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;
uniform float u_alphaCoverage;

out vec4 finalColor;

void main()
{
    if (u_useTexture != 0 && fragTexCoord.x >= 0.0) {
        float alpha = texture(texture0, fragTexCoord).a;
        // Derivative-based conservative coverage is the shader equivalent of
        // a restrained alpha-to-coverage footprint for this depth-as-color
        // target. It expands only the sampled atlas edge at minification.
        float conservativeEdge = fwidth(alpha) * u_alphaCoverage;
        if (alpha + conservativeEdge < u_alphaCutoff)
            discard;
    }
    finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
