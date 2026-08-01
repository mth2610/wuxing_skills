#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_depthSigma;

void main() {
    float center = texture(texture0, fragTexCoord).r;
    if (center >= 0.99999) { finalColor = vec4(1.0, 0.0, 0.0, 1.0); return; }
    float sum = 0.0;
    float weightSum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float sampleDepth = texture(texture0, fragTexCoord + u_direction * u_texel * float(i)).r;
        if (sampleDepth >= 0.99999) continue;
        float spatial = exp(-0.5 * float(i*i) / 6.0);
        float dz = sampleDepth - center;
        float range = exp(-0.5 * dz * dz / (u_depthSigma * u_depthSigma));
        float w = spatial * range;
        sum += sampleDepth * w;
        weightSum += w;
    }
    finalColor = vec4(weightSum > 0.0 ? sum/weightSum : center, 0.0, 0.0, 1.0);
}
