#include <stdio.h>

#include "core/gfx_quality.c"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

int main(void) {
    CHECK(GfxQuality_ParseOverride("0", GFX_HIGH) == GFX_UNLIT, "numeric UNLIT override");
    CHECK(GfxQuality_ParseOverride("low", GFX_HIGH) == GFX_LOW, "LOW name override");
    CHECK(GfxQuality_ParseOverride("MED", GFX_HIGH) == GFX_MED, "MED name is case insensitive");
    CHECK(GfxQuality_ParseOverride("high", GFX_LOW) == GFX_HIGH, "HIGH name override");
    CHECK(GfxQuality_ParseOverride("ULTRA", GFX_LOW) == GFX_ULTRA, "ULTRA name override");
    CHECK(GfxQuality_ParseOverride("4", GFX_LOW) == GFX_ULTRA, "ULTRA numeric override");
    CHECK(GfxQuality_ParseOverride("invalid", GFX_MED) == GFX_MED,
          "invalid override must retain the platform fallback");
    CHECK(GfxQuality_ParseOverride("3garbage", GFX_LOW) == GFX_LOW,
          "numeric override must match the complete value");
    CHECK(GfxQuality_ParseOverride(NULL, GFX_LOW) == GFX_LOW,
          "missing override must retain the platform fallback");
    GfxQuality_Set(GFX_ULTRA);
    GfxQuality_SetAuto(true);
    for (int i = 0; i < 600; i++) GfxQuality_UpdateAuto(1.0f / 60.0f);
    CHECK(GfxQuality_Get() == GFX_ULTRA, "stable 60 FPS retains chosen tier");
    for (int i = 0; i < 40; i++) GfxQuality_UpdateAuto(1.0f / 40.0f);
    CHECK(GfxQuality_Get() == GFX_ULTRA, "one slow window cannot trigger a downgrade");
    for (int i = 0; i < 45; i++) GfxQuality_UpdateAuto(1.0f / 40.0f);
    CHECK(GfxQuality_Get() == GFX_HIGH, "sustained slow frames reduce Ultra to High");
    for (int i = 0; i < 200; i++) GfxQuality_UpdateAuto(1.0f / 40.0f);
    CHECK(GfxQuality_Get() == GFX_LOW, "sustained pressure reaches shadow-preserving Low");
    for (int i = 0; i < 400; i++) GfxQuality_UpdateAuto(1.0f / 30.0f);
    CHECK(GfxQuality_Get() == GFX_LOW, "Auto never turns lighting off");
    GfxQuality_Set(GFX_HIGH);
    CHECK(!GfxQuality_IsAuto(), "manual selection disables Auto");
    for (int i = 0; i < 400; i++) GfxQuality_UpdateAuto(1.0f / 30.0f);
    CHECK(GfxQuality_Get() == GFX_HIGH, "fixed quality remains fixed under load");
    GfxQuality_SetAuto(true);
    for (int i = 0; i < 100; i++) GfxQuality_UpdateAuto(0.8f);
    CHECK(GfxQuality_Get() == GFX_HIGH, "loading stalls do not demote quality");
    GfxQuality_UpdateAuto(NAN);
    GfxQuality_UpdateAuto(-1.0f);
    CHECK(GfxQuality_Get() == GFX_HIGH, "invalid timing samples are ignored");
    puts("gfx_quality_override_test: PASS");
    return 0;
}
