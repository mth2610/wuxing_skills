#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[48000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *api = "core/decals/decal_system.h";
    const char *impl = "core/decals/decal_system.c";
    const char *compose = "core/composition/common/vc_decal.inl";
    const char *shader = "core/decals/shaders/decal_material.fs";

    CHECK(Has(api, "DecalMaterialParams") &&
          Has(api, "DecalSystem_AddConformalMaterialEx") &&
          !Has(api, "SetLastConformalMaterial"),
          "Material stamp spawn is atomic; no last-spawn mutable API remains");
    CHECK(Has(api, "DecalHandle") && Has(api, "DECAL_HANDLE_INVALID") &&
          Has(api, "DecalSystem_Destroy") && Has(api, "DecalSystem_IsAlive") &&
          Has(api, "DecalSystem_SetTransform"),
          "Public lifecycle API uses generation-safe decal handles");
    CHECK(Has(impl, "Decal_MakeHandle") && Has(impl, "Decal_ResolveHandle") &&
          Has(impl, "d->generation = (d->generation + 1u)") &&
          Has(impl, "g_DecalPool[idx].generation != generation"),
          "Recycled pool slots invalidate stale handles by generation");
    CHECK(Has(compose, "VFX_ComposeDecal") &&
          Has(compose, "DecalSystem_AddConformalMaterialEx") &&
          !Has(compose, "SetLastConformalMaterial"),
          "Composition uses the atomic generic decal spawn path");
    CHECK(Has(compose, "DecalMaterial_Get") &&
          !Has(compose, "matId == VC_MAT_ICE") &&
          Has("core/vfx_surface_registry.h", "VFX_SURFACE_DECAL_FROST") &&
          Has("core/decals/decal_materials.json", "DECAL_MATERIAL_FROST"),
          "Material data selects the dedicated semantic frost surface");
    CHECK(Has("core/decals/decal_materials.json", "DECAL_MATERIAL_SCORCH") &&
          Has("core/decals/decal_materials.json", "DECAL_MATERIAL_IMPACT") &&
          Has("scripts/gen_decal_materials.py", "decal_materials.generated.inl"),
          "Decal render policy is generated from canonical material data");
    CHECK(!Has(impl, "rlDisableDepthTest") &&
          Has(impl, "Decal_BeginWorldPass") && Has(impl, "Decal_EndWorldPass") &&
          Has(impl, "rlEnableDepthTest") && Has(impl, "rlDisableDepthMask") &&
          Has(impl, "rlEnableDepthMask"),
          "World decals use one flushed helper to preserve depth state");
    CHECK(Has(impl, "static int s_renderIds[MAX_DECALS]") &&
          Has(impl, "Decal_BuildRenderQueue") &&
          Has(impl, "Decal_RenderBefore") &&
          Has(impl, "Decal_BuildRenderQueue();"),
          "Draw builds a fixed compatible-state render queue before decal passes");
    CHECK(Has(impl, "Decal_HasEmissive") &&
          Has(impl, "!emissivePass || Decal_HasEmissive(d)"),
          "Conformal emissive pass is skipped when no queued decal can emit");
    CHECK(Has(shader, "fwidth(radius)") &&
          !Has(shader, "floor(fragTexCoord * 96.0)"),
          "Decal erosion has no quantized pixel-grid boundary");
    CHECK(Has(shader, "u_baseTint") && Has(shader, "u_emissiveTint") &&
          Has(shader, "u_emissiveThreshold") && Has(shader, "u_emissiveIntensity"),
          "One material shader accepts generic base and emissive parameters");
    return failures ? 1 : 0;
}
