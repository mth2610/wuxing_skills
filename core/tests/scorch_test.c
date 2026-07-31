#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[32000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *inl = "core/composition/common/vc_scorch.inl";
    CHECK(Has(inl, "VFX_ComposeScorch") && Has(inl, "VFX_SURFACE_DECAL_SCORCH"),
          "Scorch selects its semantic surface profile");
    CHECK(!Has(inl, "assets/textures/") && Has(inl, "VFX_SurfaceRegistry_Get"),
          "Scorch owns no texture path");
    CHECK(Has(inl, "BLEND_MULTIPLIED") && Has(inl, "DecalSystem_AddConformalEx"),
          "Scorch uses an explicit char blend law and decal lifecycle");
    CHECK(Has(inl, "DecalSystem_AddConformalEx") && Has(inl, "VFX_GroundHeightFromMap"),
          "Scorch follows the terrain through the conformal stamp API");
    CHECK(Has("core/decals/decal_system.c", "DECAL_STAMP_SECTORS") &&
          Has("core/decals/decal_system.c", "d->conformalStamp ||") &&
          Has("core/decals/shaders/decal_material.fs", "u_erosion"),
          "Scorch uses an irregular subdivided stamp and erosion material shader");
    CHECK(Has(inl, "scorch_life") && Has(inl, "scorch_radius"),
          "Scorch exposes visual review tunables");
    return failures ? 1 : 0;
}
