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
    CHECK(Has(inl, "BLEND_ALPHA") && Has(inl, "DecalSystem_AddConformalEx"),
          "Scorch uses an explicit char blend law and decal lifecycle");
    CHECK(Has(inl, "DecalSystem_AddConformalEx") && Has(inl, "VFX_GroundHeightFromMap"),
          "Scorch follows the terrain through the conformal stamp API");
    CHECK(Has("core/decals/decal_system.c", "DECAL_STAMP_SECTORS") &&
          Has("core/decals/decal_system.c", "d->conformalStamp ||") &&
          Has("core/decals/shaders/decal_material.fs", "u_erosion") &&
          Has("core/decals/shaders/decal_material.fs", "1.0 - smoothstep") &&
          Has("core/decals/decal_system.c", "conformalCount >= 12") &&
          Has("core/decals/decal_system.c", "stampHeightsCached") &&
          Has("core/decals/decal_system.c", "Decal_ConformalFadeAlpha") &&
          Has("core/decals/decal_system.c", "normalY < cosf") &&
          Has("core/decals/decal_system.c", "rlDisableDepthTest") &&
          Has("core/decals/decal_system.c", "rlDisableBackfaceCulling") &&
          Has("core/decals/decal_system.c", "s_locMaterialEmissivePass") &&
          Has("core/decals/shaders/decal_material.fs", "u_emissivePass") &&
          Has("core/decals/shaders/decal_material.fs", "attenuates ONCE") &&
          Has("core/decals/shaders/decal_material.fs", "denseChar"),
          "Scorch uses an irregular subdivided stamp and erosion material shader");
    CHECK(Has(inl, "scorch_life") && Has(inl, "scorch_radius"),
          "Scorch exposes visual review tunables");
    CHECK(Has("core/composition/common/vc_impact_package.inl", "matId == VC_MAT_FIRE") &&
          Has("core/composition/common/vc_impact_package.inl", "VFX_ComposeScorch(pos"),
          "Fire impacts route their decal beat to Scorch");
    CHECK(Has("core/composition/common/vc_impact_package.inl", "VFX_SURFACE_DECAL_IMPACT") &&
          Has("core/composition/common/vc_impact_package.inl", "DecalSystem_AddConformalEx") &&
          !Has("core/composition/common/vc_impact_package.inl", "SpawnGroundDecal(preset"),
          "Non-fire impacts use the semantic conformal material primary, not a legacy preset");
    CHECK(Has("core/composition/common/vc_impact_package.inl", "impact_surface_life") &&
          Has("core/composition/common/vc_impact_package.inl", "impact_surface_radius") &&
          Has("core/composition/common/vc_impact_package.inl", "surface->fadeInSeconds") &&
          Has("core/composition/common/vc_impact_package.inl", "surface->maxSlopeDegrees") &&
          Has("assets/vfx_surface_profiles.json", "impact_material_v1.png") &&
          Has("assets/vfx_surface_profiles.json", "\"approval\": \"preview_only\""),
          "Impact preview exposes lifecycle tunables and an owner-approved registry source");
    return failures ? 1 : 0;
}
