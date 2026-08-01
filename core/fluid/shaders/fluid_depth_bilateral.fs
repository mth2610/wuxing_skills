#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_depthSigma;
uniform int u_fillHoles;

void main() {
    float center = texture(texture0, fragTexCoord).r;
    // At half resolution a small particle kernel can miss a texel entirely.
    // Seed only immediate holes once, using the closest captured depth; later
    // bilateral passes never grow the surface into the background.
    if (center >= 0.99999 && u_fillHoles != 0) {
        float nearest = 1.0;
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
            nearest = min(nearest, texture(texture0, fragTexCoord + vec2(x, y)*u_texel).r);
        center = nearest;
    }
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
