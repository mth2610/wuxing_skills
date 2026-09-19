#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[180000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *aura = "core/composition/common/vc_character_aura.inl";
    const char *manifest = "assets/vfx_surface_profiles.json";

    CHECK(Has(manifest, "VFX_SURFACE_PLASMA_WISPS_NIAGARA") &&
          Has(manifest, "assets/textures/vfx/flipbooks/plasma_wisps_8x8.png"),
          "plasma wisps have a semantic surface contract");
    CHECK(Has(aura, "VFX_SurfaceRegistry_Get(VFX_SURFACE_PLASMA_WISPS_NIAGARA)") &&
          !Has(aura, "s_smokePuffTex"),
          "CharacterAura owns its wisp dependency instead of borrowing SmokePuff state");
    CHECK(Has(aura, "profile->flipbookColumns") &&
          Has(aura, "profile->flipbookRows") &&
          Has(aura, "profile->flipbookFrames") &&
          Has(aura, "&s_auraWispAnim"),
          "aura animation derives its grid from the semantic profile");
    CHECK(Has(aura, "AURA_BODY_WISP_LIVE_MAX 10") &&
          Has(aura, "AURA_GROUND_WISP_LIVE_MAX 4") &&
          Has(aura, "AURA_WISP_LIFE_AVG"),
          "whole-wisp populations have explicit live-count budgets");
    return failures ? 1 : 0;
}
