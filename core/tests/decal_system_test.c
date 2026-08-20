#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    /* Reads the WHOLE file. It used to read the first N bytes into a fixed
     * buffer, and that silently degrades: the day the implementation file grows
     * past N, assertions about anything below that offset start failing with no
     * hint that truncation — rather than the code — is the cause. It happened
     * here on 20/08/2026 at 48000 bytes. See core/docs/LANDMINES.md. */
    FILE *file = fopen(path, "rb");
    char *text;
    long size;
    size_t count;
    int found;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    size = ftell(file);
    if (size < 0) { fclose(file); return 0; }
    rewind(file);
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(file); return 0; }
    count = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[count] = '\0';
    found = (strstr(text, needle) != NULL);
    free(text);
    return found;
}

static int Exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
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
    CHECK(Has("core/decals/decal_materials.json",
              "\"surface\": \"VFX_SURFACE_DECAL_IMPACT\"") &&
          Has(compose, "VFX_SurfaceRegistry_Get(decal->surface)") &&
          Has("core/vfx_surface_registry.generated.inl",
              ".bodyPath = \"assets/textures/surfaces/impact_material_v2.png\"") &&
          Exists("assets/textures/surfaces/impact_material_v2.png"),
          "Generic impact decals resolve the authored fracture texture through the semantic registry");
    CHECK(Has("core/decals/decal_materials.json", "\"priority\"") &&
          Has("core/decals/decal_material.h", "int priority") &&
          Has(compose, ".priority = decal->priority") &&
          Has(impl, "incomingPriority < lowestPriority") &&
          Has(impl, "Decal_ClampPriority"),
          "Material priority controls conformal-budget admission and eviction");
    CHECK(Has("core/decals/decal_materials.json", "\"max_draw_distance\"") &&
          Has("core/decals/decal_material.h", "maxDrawDistance") &&
          Has(impl, "Decal_IsWithinDrawDistance") &&
          Has(impl, "s_renderDistanceCulledCount"),
          "Material draw-distance policy is applied conservatively in the queue");
    CHECK(Has(impl, "FindSlot(int incomingPriority)") &&
          Has(impl, "bool culled = !Decal_IsVisible(candidate)") &&
          Has(impl, "incomingPriority < g_DecalPool[target].material.priority") &&
          Has(impl, "return DECAL_HANDLE_INVALID;"),
          "Pool pressure preserves higher-priority live decals before spawning");
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
          Has(impl, "!emissivePass || Decal_HasEmissive(d, s_renderLod"),
          "Conformal emissive pass is skipped when no queued decal can emit");
    CHECK(Has(impl, "Decal_IsVisible") && Has(impl, "Decal_BoundsRadius") &&
          Has(impl, "CAMERA_ORTHOGRAPHIC") && Has(impl, "s_hasCamera") &&
          Has(impl, "Decal_IsVisible(d)"),
          "Queue uses conservative camera-frustum admission when camera data exists");
    CHECK(Has(api, "DecalRenderStats") && Has(api, "DecalSystem_GetRenderStats") &&
          Has(impl, "outStats->visible = s_renderCount") &&
          Has(impl, "outStats->culled = s_renderCulledCount") &&
          Has(impl, "outStats->lodCulled = s_renderLodCulledCount"),
          "Read-only render statistics expose CPU culling results");
    CHECK(Has(impl, "Decal_SelectLod") && Has(impl, "diameterPixels < 12.0f") &&
          Has(impl, "Decal_DrawConformalMesh") &&
          Has(impl, "lod < 2 && d->material.emissiveIntensity"),
          "Projected-size LOD removes tiny decals and reduces conformal work");
    CHECK(Has(impl, "Decal_EstimatedCoverage") && !Has(impl, "s_renderBaseCoverage + coverage > 0.20f") &&
          Has(impl, "s_renderEmissiveCoverage + coverage <= 0.08f") &&
          Has(api, "emissiveSuppressed") && Has(api, "emissiveCoverage"),
          "Base decals remain visible while only excess emissive coverage is suppressed");
    CHECK(Has(shader, "fwidth(radius)") &&
          !Has(shader, "floor(fragTexCoord * 96.0)"),
          "Decal erosion has no quantized pixel-grid boundary");
    CHECK(Has(shader, "u_baseTint") && Has(shader, "u_emissiveTint") &&
          Has(shader, "u_emissiveThreshold") && Has(shader, "u_emissiveIntensity"),
          "One material shader accepts generic base and emissive parameters");
    CHECK(!Has(shader, "u_erosion") && Has(shader, "fragColor.r * 0.24") &&
          Has(impl, "Decal_ApplyMaterialBucket") && Has(impl, "Decal_SameMaterialBucket") &&
          Has(impl, "Decal_CompareMaterialBucket") && Has(impl, "int materialOrder") &&
          Has(impl, "rlColor4ub((unsigned char)(erosion * 255.0f)") &&
          !Has(impl, "s_locMaterialErosion"),
          "Conformal erosion is vertex data; uniforms change only by material bucket");
    CHECK(Has(impl, "unsigned int bucketTextureId") && Has(impl, "bool materialChanged") &&
          Has(impl, "if (drawing)") && Has(impl, "Decal_DrawConformalMesh(d"),
          "Adjacent conformal meshes share one material/texture triangle submission");
    CHECK(Has("core/decals/shaders/decal_flow.fs", "in vec3 fragFlow") &&
          Has("core/decals/shaders/decal_flow.fs", "fragFlow.x") &&
          !Has("core/decals/shaders/decal_flow.fs", "u_flowSpeed") &&
          Has(impl, "rlNormal3f(d->flowScroll ? d->flowSpeed") &&
          Has(impl, "ResourceManager_LoadShader(\"core/decals/shaders/decal_material.vs\"") &&
          !Has(impl, "s_locFlowSpeed"),
          "Legacy flow parameters use vertex data instead of per-decal uniforms");
    CHECK(Has(api, "conformalSubmissions") && Has(api, "materialSwitches") &&
          Has(impl, "++s_renderConformalSubmissions") &&
          Has(impl, "++s_renderMaterialSwitches"),
          "Render statistics expose submission and state-switch pressure");
    return failures ? 1 : 0;
}
