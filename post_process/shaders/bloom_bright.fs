#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_threshold;
uniform float u_exposure;
uniform vec2 u_sourceTexelSize;
uniform float u_maxEnergy;
uniform float u_knee;

out vec4 finalColor;

void main() {
    float expVal = max(u_exposure, 0.0001);
    float rawThreshold = u_threshold / expVal;
    float knee = (u_knee > 0.0) ? u_knee : 0.15;
    float rawKneeFloor = max(0.0, (u_threshold - knee) / expVal);

    vec3 peakCol = vec3(0.0);
    vec3 weightedCol = vec3(0.0);
    float weightSum = 0.0;
    float bestRawBrightness = 0.0;
    float brightCount = 0.0;

    // 9-tap (3x3) bilinear-assisted prefilter across the 4x4 source cell:
    // preserves 1-pixel hot HDR emitters while blending bright neighbors to prevent
    // sub-pixel hue/intensity snapping during camera movement.
    for (int y = -1; y <= 1; ++y) {
        float offsetY = float(y) * 1.15 * u_sourceTexelSize.y;
        for (int x = -1; x <= 1; ++x) {
            vec2 cellOffset = vec2(float(x) * 1.15 * u_sourceTexelSize.x, offsetY);
            vec3 sampleColor = clamp(texture(texture0, fragTexCoord + cellOffset).rgb,
                                     vec3(0.0), vec3(65000.0));

            float maxComp = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
            float luma = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
            float rawBrightness = mix(maxComp, luma, 0.2);

            if (rawBrightness > bestRawBrightness) {
                bestRawBrightness = rawBrightness;
                peakCol = sampleColor;
            }

            float tapExcess = max(0.0, rawBrightness - rawKneeFloor);
            // Karis-style damping on accumulation so a single firefly doesn't skew hue
            float tapWeight = tapExcess / (1.0 + rawBrightness * 0.25);
            weightedCol += sampleColor * tapWeight;
            weightSum += tapWeight;

            brightCount += step(rawThreshold, rawBrightness);
        }
    }

    float brightness = bestRawBrightness * expVal;
    float soft = brightness - u_threshold + knee;
    if (soft <= 0.0) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Pha trộn giữa màu đỉnh (giữ độ nảy cho tia lửa 1-pixel) và màu trung bình vùng sáng (mượt biên)
    vec3 avgBrightCol = (weightSum > 1e-5) ? (weightedCol / weightSum) : peakCol;
    vec3 col = mix(avgBrightCol, peakCol, 0.65);

    float coverage = sqrt(max(brightCount * 0.111111, 0.0625));

    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);

    float weight = max(soft, brightness - u_threshold) / max(brightness, 0.00001);

    // Soft ceiling (Reinhard-shaped asymptote)
    vec3 brightColor = col * (weight * 2.2 * coverage);
    float maxEnergy = (u_maxEnergy > 0.0) ? u_maxEnergy : 12.0;
    float energy = max(brightColor.r, max(brightColor.g, brightColor.b));
    brightColor *= maxEnergy / (maxEnergy + energy);

    finalColor = vec4(brightColor, 1.0);
}