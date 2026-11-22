#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform sampler2D u_depthTex;
uniform vec2 u_texelSize;

void main()
{
    float centerDepth = texture(u_depthTex, fragTexCoord).r;
    if (centerDepth <= 0.01 || centerDepth >= 220.0) {
        finalColor = vec4(1.0);
        return;
    }

    float sum = 0.0;
    float wSum = 0.0;
    const vec2 kOffsets[5] = vec2[5](
        vec2( 0.0,  0.0),
        vec2(-1.0,  0.0),
        vec2( 1.0,  0.0),
        vec2( 0.0, -1.0),
        vec2( 0.0,  1.0)
    );
    const float kWeights[5] = float[5](0.36, 0.16, 0.16, 0.16, 0.16);

    for (int i = 0; i < 5; i++) {
        vec2 uv = clamp(fragTexCoord + kOffsets[i] * u_texelSize, vec2(0.001), vec2(0.999));
        float d = texture(u_depthTex, uv).r;
        float diff = abs(d - centerDepth) / max(centerDepth * 0.08, 0.15);
        float w = kWeights[i] * exp(-diff * diff);
        sum += texture(texture0, uv).r * w;
        wSum += w;
    }

    float ao = clamp(sum / max(wSum, 1e-4), 0.25, 1.0);
    finalColor = vec4(ao, ao, ao, 1.0);
}
