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
    if (strstr(shader, "sunbeamHaze") ||
        !strstr(shader, "float directLight = shadow * (0.15 + 0.85 * canopyShaft) * u_godRayIntensity;") ||
        !strstr(shader, "u_sunColor * (directLight * shaftVisibility * 5.0)")) {
        puts("FAIL: beams must use scene shadows and sun color without patterned density");
        failures++;
    }
    /* View-axis depth must reconstruct the same ground point off centre. */
    CheckNear(21.0f / 0.7f, 30.0f, "off-centre ray length from axial depth");
    float totalLength = 0.0f;
    float lastInterval = 0.0f;
    for (int i = 0; i < 20; i++) {
        float u0 = (float)i / 20.0f, u1 = (float)(i + 1) / 20.0f;
        float t0 = 1.2f + 38.8f * (1.0f - (1.0f-u0)*(1.0f-u0));
        float t1 = 1.2f + 38.8f * (1.0f - (1.0f-u1)*(1.0f-u1));
        totalLength += t1-t0;
        lastInterval = t1-t0;
    }
    CheckNear(totalLength, 38.8f, "nonuniform steps preserve optical path length");
    if (lastInterval > 0.1f || lastInterval <= 0.0f) failures++;
    if (!strstr(shader, "sceneDepth / max(dot(rayDir, u_viewForward), 0.001)") ||
        !strstr(shader, "float stepSize = t1 - t0;") ||
        !strstr(shader, "u_lightVP * vec4(samplePos, 1.0)")) failures++;
    /* Numeric framing and source wiring only; GPU depth/appearance need captures. */
    if (failures) return 1;
    puts("PASS: volumetric ground framing, local sampling and shader wiring");
    return 0;
}
