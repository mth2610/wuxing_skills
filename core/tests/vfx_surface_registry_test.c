#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[160000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *manifest = "assets/vfx_surface_profiles.json";
    const char *header = "core/vfx_surface_registry.h";
    const char *source = "core/vfx_surface_registry.c";
    CHECK(Has(header, "VFX_SURFACE_SMOKE_RIBBON") &&
          Has(header, "VFX_SURFACE_ENERGY_RIBBON") &&
          Has(header, "VFX_SURFACE_ENERGY_TUBE") &&
          Has(header, "VFX_SURFACE_SMOKE_PUFF") &&
          Has(header, "VFX_SURFACE_FIRE_TONGUE") &&
          Has(header, "VFX_SURFACE_DECAL_RESIDUE") &&
          Has(header, "VFX_SURFACE_DECAL_SCORCH"),
          "P1 exposes every required semantic primary surface");
    CHECK(Has(header, "VFX_SurfaceRegistry_Get") && Has(source, "ResourceManager_LoadTexture"),
          "registry is the only runtime loader for profile assets");
    CHECK(Has(manifest, "\"primitive\"") && Has(manifest, "\"channels\"") &&
          Has(manifest, "\"seam\"") && Has(manifest, "\"provenance\"") &&
          Has(manifest, "\"role\"") && Has(manifest, "\"projection\"") &&
          Has(manifest, "\"lifecycle\"") && Has(manifest, "\"budget\""),
          "manifest records semantic role, material contract, lifecycle and budget");
    CHECK(Has(manifest, "\"decal_residue_material_blocked\"") &&
          Has(manifest, "\"decal_scorch_material_preview\"") &&
          Has(manifest, "\"blocked_visual_owner\"") &&
          Has(manifest, "\"preview_only\"") &&
          Has(manifest, "\"fallback_candidates\"") &&
          Has(header, "VFX_SURFACE_PRIMITIVE_DECAL"),
          "P4 blocks residue while Scorch uses an owner-approved review source only");
    CHECK(Has("core/composition/common/vc_smoke_puff.inl", "VFX_SurfaceRegistry_Get(VFX_SURFACE_SMOKE_PUFF)") &&
          Has("core/composition/fire/flame_volume.inl", "VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_TONGUE)") &&
          Has("core/composition/common/vc_core_smoketrail.inl", "VFX_SurfaceRegistry_Get(VFX_SURFACE_SMOKE_RIBBON)") &&
          Has("core/composition/common/vc_ribbon_trail.inl", "VFX_SurfaceRegistry_Get(VFX_SURFACE_ENERGY_RIBBON)") &&
          Has("core/composition/common/vc_volume_trail.inl", "VFX_SURFACE_ENERGY_TUBE"),
          "live compositions request semantic profiles instead of paths");
    CHECK(!Has("core/composition/common/vc_volume_trail.inl", "k_volSheetPath") &&
          !Has("core/composition/common/vc_core_smoketrail.inl", "ResourceManager_LoadTexture(\"assets/textures/smoke_ribbon") &&
          !Has("core/composition/common/vc_ribbon_trail.inl", "LoadImage(profile->bodyPath)"),
          "migrated trail consumers retain no local texture path table");
    return failures ? 1 : 0;
}
