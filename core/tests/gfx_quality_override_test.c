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
    CHECK(GfxQuality_ParseOverride("invalid", GFX_MED) == GFX_MED,
          "invalid override must retain the platform fallback");
    CHECK(GfxQuality_ParseOverride("3garbage", GFX_LOW) == GFX_LOW,
          "numeric override must match the complete value");
    CHECK(GfxQuality_ParseOverride(NULL, GFX_LOW) == GFX_LOW,
          "missing override must retain the platform fallback");
    puts("gfx_quality_override_test: PASS");
    return 0;
}
