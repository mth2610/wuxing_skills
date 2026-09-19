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
    const char *flare = "core/composition/common/vc_optical_flare.inl";
    const char *manifest = "assets/vfx_surface_profiles.json";

    CHECK(Has(manifest, "VFX_SURFACE_LENS_FLARE_STAR_NIAGARA") &&
          Has(manifest, "VFX_SURFACE_LENS_FLARE_STREAK_NIAGARA"),
          "extracted star and anamorphic streak own semantic surface profiles");
    CHECK(Has(flare, "VFX_SurfaceRegistry_Get(VFX_SURFACE_LENS_FLARE_STAR_NIAGARA)") &&
          Has(flare, "VFX_SurfaceRegistry_Get(VFX_SURFACE_LENS_FLARE_STREAK_NIAGARA)"),
          "OpticalFlare resolves both extracted layers through the registry");
    CHECK(Has(flare, "TimeFX_Elapsed()") && !Has(flare, "GetTime()"),
          "flare breathing uses the scaled deterministic VFX clock");
    CHECK(Has(flare, "OptFlare_GetStreakTexture()") &&
          Has(flare, "rlSetTexture(streakTex.id)"),
          "the extracted anamorphic streak is a rendered flare layer");
    CHECK(Has(flare, "rlDrawRenderBatchActive();\n    BeginBlendMode") &&
          Has(flare, "rlDrawRenderBatchActive();\n    rlEnableDepthTest"),
          "manual flare state transitions flush the deferred batch");
    return failures ? 1 : 0;
}
