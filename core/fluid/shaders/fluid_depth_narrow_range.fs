#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;
uniform vec2 u_direction;
uniform float u_depthRange;
uniform float u_kernelRadius;
uniform int u_filterRadius;
uniform int u_fillHoles;
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;

float ViewDistance(float deviceDepth) {
    vec4 clip=vec4(0.0,0.0,deviceDepth*2.0-1.0,1.0);
    vec4 view=u_inverseProjection*clip;
    return max(0.0001,-view.z/view.w);
}

float DeviceDepth(float viewDistance) {
    vec4 clip=u_projection*vec4(0.0,0.0,-viewDistance,1.0);
    return clip.z/clip.w*0.5+0.5;
}

void AccumulateSample(float sampleDeviceDepth, float spatialWeight,
                      inout float weightedDepth, inout float weightSum,
                      float centerDistance) {
    if (sampleDeviceDepth >= 0.99999) return;
    float sampleDistance = ViewDistance(sampleDeviceDepth);
    float deltaZ = sampleDistance - centerDistance;
    
    // Bilateral range weighting: samples within fluid depth range belong to the
    // continuous liquid sheet and contribute smoothly. Samples outside the range
    // (background or distant fluid layers) fall off exponentially to 0 weight,
    // avoiding artificial depth step rings or boundary contour artifacts.
    float sigmaR = max(u_depthRange * 0.85, 0.004);
    float normDelta = deltaZ / sigmaR;
    float rangeWeight = exp(-0.5 * normDelta * normDelta);
    
    // Reject samples belonging to a completely separate foreground layer
    if (deltaZ < -u_depthRange * 1.5) return;
    // Suppress background or distant sheet samples smoothly
    if (deltaZ > u_depthRange * 2.5) rangeWeight *= 0.05;

    float weight = spatialWeight * rangeWeight;
    weightedDepth += sampleDistance * weight;
    weightSum += weight;
}

void main() {
    float centerDevice = texture(texture0, fragTexCoord).r;

    /* Seed a 5x5 capture hole to bridge gaps between densely packed small
     * particles (e.g. force-field orb at high particle count). The wider
     * region joins overlapping kernels without growing isolated droplets. */
    if (centerDevice >= 0.99999 && u_fillHoles != 0) {
        float nearest = 1.0;
        for (int y = -2; y <= 2; y++) {
            for (int x = -2; x <= 2; x++) {
                nearest = min(nearest, texture(texture0, fragTexCoord + vec2(x, y) * u_texel).r);
            }
        }
        centerDevice = nearest;
    }
    if (centerDevice >= 0.99999) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    float centerDistance = ViewDistance(centerDevice);
    float weightedDepth = centerDistance;
    float weightSum = 1.0;

    // Loop up to u_filterRadius (capped at 16 to satisfy GLSL constant loops).
    for (int i = 1; i <= 16; i++) {
        if (i > u_filterRadius) break;
        float fi = float(i);
        // Wider Gaussian sigma (6.0) gives softer roll-off across gaps
        // between adjacent small particles, preventing dark valleys.
        float spatialWeight = exp(-0.5 * fi * fi / 6.0);
        float positive = texture(texture0, fragTexCoord + u_direction * u_texel * fi).r;
        float negative = texture(texture0, fragTexCoord - u_direction * u_texel * fi).r;
        AccumulateSample(positive, spatialWeight, weightedDepth, weightSum, centerDistance);
        AccumulateSample(negative, spatialWeight, weightedDepth, weightSum, centerDistance);
    }

    float filteredDistance = weightedDepth / max(weightSum, 0.0001);
    finalColor = vec4(clamp(DeviceDepth(filteredDistance), 0.0, 1.0), 0.0, 0.0, 1.0);
}
