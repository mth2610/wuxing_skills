#ifndef VOLUMETRIC_FOG_DISTANCE_H
#define VOLUMETRIC_FOG_DISTANCE_H

// A distant atmosphere must begin beyond the camera's focus point at every zoom.
// Profiles with a near cutoff <= 3 m keep their existing local-mist behavior.
static inline float VolumetricFog_EffectiveStart(float configuredStart,
                                                 float cameraFocusDistance) {
    float start = configuredStart > 0.0f ? configuredStart : 1.2f;
    if (start > 3.0f) {
        float focusClearance = cameraFocusDistance + 7.0f;
        if (focusClearance > start) start = focusClearance;
    }
    return start;
}

#endif
