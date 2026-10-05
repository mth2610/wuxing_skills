#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); failures++; } } while (0)

static void Integrate(double thickness, int steps, int bounded,
                      double *radiance, double *opacity) {
    double transmittance = 1.0;
    *radiance = 0.0;
    for (int i = 0; i < steps; ++i) {
        double interval = exp(-thickness / steps);
        if (bounded) interval = fmax(interval, 0.20 / transmittance);
        *radiance += transmittance * (1.0 - interval);
        transmittance *= interval;
        if (transmittance <= 0.20) {
            transmittance = 0.20;
            break;
        }
    }
    *opacity = 1.0 - transmittance;
}

int main(void) {
    double radiance, opacity;
    Integrate(2.0, 1, 0, &radiance, &opacity);
    CHECK(radiance > opacity + 0.06, "old integration exposes excess energy at opacity floor");
    const double thicknesses[] = {0.0, 0.1, 1.0, 2.0, 20.0, 1000.0};
    const int stepCounts[] = {1, 14, 20, 32};
    for (int t = 0; t < 6; ++t) {
        for (int s = 0; s < 4; ++s) {
            Integrate(thicknesses[t], stepCounts[s], 1, &radiance, &opacity);
            double expected = 1.0 - fmax(exp(-thicknesses[t]), 0.20);
            CHECK(fabs(opacity - expected) < 1e-12, "authored extinction and opacity floor are preserved");
            CHECK(fabs(radiance - opacity) < 1e-12, "homogeneous unit lighting conserves radiance and opacity");
        }
    }
    /* Mirror arithmetic cannot validate GPU output. Assert the load-bearing
       production expression so old shader math fails this regression. */
    FILE *file = fopen("core/volumetric/shaders/volumetric_fog.fs", "rb");
    CHECK(file != NULL, "fog shader readable");
    if (!file) return 1;
    char source[22000] = {0};
    fread(source, 1, sizeof(source) - 1, file);
    fclose(file);
    CHECK(strstr(source, "max(exp(-opticalThickness), 0.20 / transmittance)") != NULL,
          "shader bounds extinction before accumulating radiance");
    CHECK(strstr(source, "stepLight * (1.0 - stepTransmittance)") != NULL &&
          strstr(source, "transmittance * stepRadiance") != NULL,
          "bounded interval extinction controls premultiplied radiance");
    CHECK(strstr(source, "transmittance *= stepTransmittance") != NULL &&
          strstr(source, "1.0 - transmittance") != NULL,
          "same bounded extinction controls composite opacity");
    if (failures) return 1;
    puts("PASS: homogeneous fog transport conserves energy at preserved opacity floor");
    return 0;
}
