#include "core/volumetric/volumetric_fog_distance.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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
    CheckNear(VolumetricFog_EffectiveStart(22.0f, 10.0f), 1.2f,
              "distant framing samples nearby local volumes");
    CheckNear(VolumetricFog_EffectiveStart(22.0f, 32.0f), 1.2f,
              "framing does not exclude the visible ground at far zoom");
    CheckNear(VolumetricFog_EffectiveStart(0.0f, 10.0f), 1.2f,
              "unset cutoff keeps fallback");

    CheckNear(VolumetricFog_GroundSpan(3.0f, -0.6f), 5.0f, "default ground half-span");
    CheckNear(VolumetricFog_GroundSpan(9.0f, -0.6f), 15.0f, "far zoom scales ground half-span");
    CheckNear(VolumetricFog_GroundSpan(3.0f, 0.0f), 12.0f, "horizontal view stays finite");
    /* Same relative ground placement at 3x zoom must produce the same fade. */
    CheckNear(2.5f / VolumetricFog_GroundSpan(3.0f, -0.6f),
              7.5f / VolumetricFog_GroundSpan(9.0f, -0.6f), "zoom-invariant framing");
    char shader[18000] = {0};
    FILE *file = fopen("core/volumetric/shaders/volumetric_fog.fs", "rb");
    if (!file) return 1;
    fread(shader, 1, sizeof(shader) - 1, file);
    fclose(file);
    if (!strstr(shader, "dot(receiverPos - u_fogFocus, u_fogForward)") ||
        !strstr(shader, "behindFocus / max(u_fogSpan, 0.001)") ||
        !strstr(shader, "+ localDensity * localFade")) failures++;
    /* Numeric framing and source wiring only; GPU depth/appearance need captures. */
    if (failures) return 1;
    puts("PASS: volumetric ground framing, local sampling and shader wiring");
    return 0;
}
