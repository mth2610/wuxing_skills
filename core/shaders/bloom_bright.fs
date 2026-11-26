#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D u_exposureTex;
uniform float u_threshold;
uniform float u_exposure;
uniform float u_autoExposure;
uniform vec2 u_sourceTexelSize;
uniform float u_maxEnergy;
uniform float u_knee;

out vec4 finalColor;

void main() {
    float baseExp = max(u_exposure, 0.0001);
    float expAchromatic = baseExp;
    if (u_autoExposure > 0.0) {
        float autoE = texture(u_exposureTex, vec2(0.5)).r;
        if (!isnan(autoE) && !isinf(autoE) && autoE > 0.0) {
            expAchromatic *= mix(1.0, clamp(autoE, 0.1, 10.0), u_autoExposure);
        }
    }
    float knee = (u_knee > 0.0) ? u_knee : 0.15;

    vec3 peakCol = vec3(0.0);
    vec3 weightedCol = vec3(0.0);
    float weightSum = 0.0;
    float bestRawBrightness = 0.0;
    float bestEffectiveBrightness = 0.0;
    float brightCount = 0.0;

    // 5-tap Karis cross (center + 4 diagonal bilinear taps across the 4x4 cell):
    // preserves 1-pixel hot HDR emitters while cutting prefilter texture fetches by 44%.
    const vec2 kOffsets[5] = vec2[5](
        vec2( 0.00,  0.00),
        vec2(-0.85, -0.85),
        vec2( 0.85, -0.85),
        vec2(-0.85,  0.85),
        vec2( 0.85,  0.85)
    );
    for (int i = 0; i < 5; ++i) {
        vec2 cellOffset = kOffsets[i] * u_sourceTexelSize;
        vec3 sampleColor = clamp(texture(texture0, fragTexCoord + cellOffset).rgb,
                                 vec3(0.0), vec3(65000.0));

        float maxComp = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
        float minComp = min(sampleColor.r, min(sampleColor.g, sampleColor.b));
        float sampleSat = (maxComp > 1e-4) ? clamp((maxComp - minComp) / maxComp, 0.0, 1.0) : 0.0;
        float luma = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
        float rawBrightness = mix(maxComp, luma, 0.2);

        // Apply autoExposure dimming only to near-SDR achromatic diffuse snow/sand
        // (rawBrightness * baseExp <= 1.35) so bright terrain never self-blooms,
        // while ALL true HDR VFX emitters — including pale champagne-gold and
        // white-gold cores — evaluate at full base exposure on snow and night alike.
        float diffuseGate = 1.0 - smoothstep(1.35, 2.15, rawBrightness * baseExp);
        float achromaticMask = (1.0 - smoothstep(0.10, 0.32, sampleSat)) * diffuseGate;
        float tapExp = mix(baseExp, expAchromatic, achromaticMask);
        float achromaticLift = (1.0 - smoothstep(0.08, 0.28, sampleSat)) * diffuseGate * 0.28;
        float tapBrightness = max(0.0, rawBrightness * tapExp - achromaticLift);

        if (tapBrightness > bestEffectiveBrightness) {
            bestEffectiveBrightness = tapBrightness;
            bestRawBrightness = rawBrightness;
            peakCol = sampleColor;
        }

        float tapFloor = max(0.0, u_threshold - knee);
        float tapExcess = max(0.0, tapBrightness - tapFloor);
        // Weight chromaticity toward saturated corona taps so a bleached white-hot
        // 1px core surrounded by colored plasma radiates a saturated elemental halo.
        float chromaBoost = 1.0 + 2.2 * sampleSat;
        float tapWeight = (tapExcess * chromaBoost) / (1.0 + rawBrightness * 0.25);
        weightedCol += sampleColor * tapWeight;
        weightSum += tapWeight;

        brightCount += step(u_threshold, tapBrightness);
    }

    float brightness = bestEffectiveBrightness;
    float soft = brightness - u_threshold + knee;
    if (soft <= 0.0) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Decouple thin-emitter energy from bleached-core whiteness: keep peakCol's
    // HDR magnitude while inheriting the saturated corona's hue when peakCol is white.
    vec3 avgBrightCol = (weightSum > 1e-5) ? (weightedCol / weightSum) : peakCol;
    float peakMax = max(peakCol.r, max(peakCol.g, peakCol.b));
    float peakMin = min(peakCol.r, min(peakCol.g, peakCol.b));
    float peakSat = (peakMax > 1e-4) ? clamp((peakMax - peakMin) / peakMax, 0.0, 1.0) : 0.0;
    float avgMax  = max(avgBrightCol.r, max(avgBrightCol.g, avgBrightCol.b));
    vec3 chromaMatchedPeak = (avgMax > 1e-4) ? (avgBrightCol * (peakMax / avgMax)) : peakCol;
    vec3 col = mix(chromaMatchedPeak, peakCol, mix(0.22, 0.65, peakSat));

    float coverage = sqrt(max(brightCount * 0.20, 0.0625));

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