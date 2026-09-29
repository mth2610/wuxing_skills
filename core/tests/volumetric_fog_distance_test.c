#include "core/volumetric/volumetric_fog_distance.h"
#include <math.h>
#include <stdio.h>

static int failures = 0;

static void CheckNear(float actual, float expected, const char *label) {
    if (fabsf(actual - expected) > 0.0001f) {
        printf("FAIL: %s (%.3f, expected %.3f)\n", label, actual, expected);
        failures++;
    }
}

int main(void) {
    CheckNear(VolumetricFog_EffectiveStart(1.0f, 24.0f), 1.0f,
              "legacy nearby fog remains available");
    CheckNear(VolumetricFog_EffectiveStart(22.0f, 10.0f), 22.0f,
              "distant fog uses authored cutoff at close zoom");
    CheckNear(VolumetricFog_EffectiveStart(22.0f, 32.0f), 39.0f,
              "distant fog clears the camera focus at far zoom");
    CheckNear(VolumetricFog_EffectiveStart(0.0f, 10.0f), 1.2f,
              "unset cutoff keeps fallback");

    if (failures) return 1;
    puts("PASS: volumetric distance cutoff and zoom clearance");
    return 0;
}
