#ifndef VOLUMETRIC_FOG_DISTANCE_H
#define VOLUMETRIC_FOG_DISTANCE_H

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
