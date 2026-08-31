#include <math.h>
#include <stdio.h>

#include "core/gas/gas_optics_internal.inl"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_optical_controls_test: check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int Near(float a, float b) { return fabsf(a - b) < 0.0001f; }

int main(void) {
    GasOpticalControls smoke = GasOpticalControls_Resolve(0, 0.0f, 0.0f, 0.0f);
    GasOpticalControls fire = GasOpticalControls_Resolve(1, 0.0f, 0.0f, 0.0f);
    GasOpticalControls energy = GasOpticalControls_Resolve(2, 0.0f, 0.0f, 0.0f);
    CHECK(smoke.shadowStrength > fire.shadowStrength);
    CHECK(fire.shadowStrength > energy.shadowStrength);
    CHECK(smoke.detailStrength < fire.detailStrength);
    CHECK(fire.detailStrength < energy.detailStrength);
    CHECK(Near(smoke.backgroundAdapt, 1.0f));

    GasOpticalControls authored = GasOpticalControls_Resolve(1, 5.0f, -1.0f, 2.0f);
    CHECK(Near(authored.detailStrength, 2.0f));
    CHECK(Near(authored.shadowStrength, 0.0f));
    CHECK(Near(authored.backgroundAdapt, 1.0f));
    puts("gas_optical_controls_test: PASS");
    return 0;
}
