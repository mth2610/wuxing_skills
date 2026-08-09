#include "core/vfx_contrast.h"
#include <math.h>
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

static int SameColor(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int main(void)
{
    int bad = 0;
    Color authored = {120, 180, 240, 100};
    Color noneBody = VFXContrast_ApplyColor(
        authored, VFX_CONTRAST_NONE, VFX_CONTRAST_BODY);
    Color noneEmission = VFXContrast_ApplyColor(
        authored, VFX_CONTRAST_NONE, VFX_CONTRAST_EMISSION);
    Color energyBody = VFXContrast_ApplyColor(
        authored, VFX_CONTRAST_ENERGY, VFX_CONTRAST_BODY);
    Color energyEmission = VFXContrast_ApplyColor(
        authored, VFX_CONTRAST_ENERGY, VFX_CONTRAST_EMISSION);

    CHECK(SameColor(authored, noneBody));
    CHECK(SameColor(authored, noneEmission));
    CHECK(energyBody.r < authored.r && energyBody.g < authored.g);
    CHECK(energyBody.a > authored.a);
    CHECK(energyEmission.r > authored.r && energyEmission.a > authored.a);
    CHECK(fabsf(VFXContrast_ApplyBodyOpacity(0.5f, VFX_CONTRAST_NONE) - 0.5f) < 0.0001f);
    CHECK(VFXContrast_ApplyBodyOpacity(0.9f, VFX_CONTRAST_ENERGY) == 1.0f);
    CHECK(VFXContrast_ApplyEmissionIntensity(2.0f, VFX_CONTRAST_ENERGY) > 2.0f);
    CHECK(VFXContrast_ApplyEmissionIntensity(2.0f, VFX_CONTRAST_SMOKE) == 0.0f);
    CHECK(VFXContrast_ApplyEmissionThreshold(1.1f, VFX_CONTRAST_ENERGY) < 1.0f);
    CHECK(VFXContrast_ApplyEmissionThreshold(1.1f, VFX_CONTRAST_NONE) == 1.1f);

    puts(bad ? "vfx contrast: FAIL" : "vfx contrast: PASS");
    return bad ? 1 : 0;
}
