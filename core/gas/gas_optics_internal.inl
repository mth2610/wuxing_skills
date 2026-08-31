#ifndef WUXING_GAS_OPTICS_INTERNAL_INL
#define WUXING_GAS_OPTICS_INTERNAL_INL

typedef struct GasOpticalControls {
    float detailStrength;
    float shadowStrength;
    float backgroundAdapt;
} GasOpticalControls;

static float GasOpticalControls_Clamp(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Zero preserves source compatibility by selecting the material preset.
 * Negative values explicitly disable a control; positive values are bounded
 * before reaching the shader so authored data cannot destabilize the volume. */
static GasOpticalControls GasOpticalControls_Resolve(int kind,
                                                     float detailStrength,
                                                     float shadowStrength,
                                                     float backgroundAdapt)
{
    GasOpticalControls controls = {1.0f, 0.95f, 1.0f};
    if (kind == 1) {
        controls.detailStrength = 1.15f;
        controls.shadowStrength = 0.45f;
    } else if (kind == 2) {
        controls.detailStrength = 1.25f;
        controls.shadowStrength = 0.25f;
    }
    if (detailStrength != 0.0f)
        controls.detailStrength = GasOpticalControls_Clamp(detailStrength, 0.0f, 2.0f);
    if (shadowStrength != 0.0f)
        controls.shadowStrength = GasOpticalControls_Clamp(shadowStrength, 0.0f, 2.0f);
    if (backgroundAdapt != 0.0f)
        controls.backgroundAdapt = GasOpticalControls_Clamp(backgroundAdapt, 0.0f, 1.0f);
    return controls;
}

#endif
