#ifndef VOLUMETRIC_FOG_DISTANCE_H
#define VOLUMETRIC_FOG_DISTANCE_H

#include <math.h>

static inline float VolumetricFog_ClampDistantCoverage(float coverage) {
    if (isnan(coverage)) return 1.0f;
    return coverage < 0.0f ? 0.0f : coverage > 1.0f ? 1.0f : coverage;
}

// Compress only the legacy distant region toward the far ground edge (depth=1).
// This mirrors the shader's framing; it does not alter local fog or extinction.
static inline float VolumetricFog_DistantDepth(float normalizedDepth, float coverage) {
    coverage = VolumetricFog_ClampDistantCoverage(coverage);
    if (coverage <= 0.0f) return -1.0f;
    if (coverage >= 1.0f) return normalizedDepth;
    return 1.0f + (normalizedDepth - 1.0f) / fmaxf(coverage, 0.0001f);
}

// Distant profiles use focus-relative ground framing in the shader. Start the
// march nearby so authored local volumes are sampled independently of haze.
static inline float VolumetricFog_EffectiveStart(float configuredStart,
                                                 float cameraFocusDistance) {
    (void)cameraFocusDistance;
    return configuredStart > 3.0f ? 1.2f
         : configuredStart > 0.0f ? configuredStart : 1.2f;
}

// Ground half-span projected onto the horizontal viewing direction (metres).
static inline float VolumetricFog_GroundSpan(float halfViewHeight, float verticalView) {
    float pitch = verticalView < 0.0f ? -verticalView : verticalView;
    return halfViewHeight / (pitch > 0.25f ? pitch : 0.25f);
}

#endif
