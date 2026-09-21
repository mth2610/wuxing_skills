#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[180000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    const char *header = "core/composition/visual_composer.h";
    const char *emitter = "core/composition/common/vc_mesh_particle_emitter.inl";
    const char *aura = "core/composition/common/vc_mesh_surface_aura.inl";
    const char *auraVs = "core/shaders/mesh_surface_aura.vs";

    CHECK(!Has("core/composition/common/vc_character_aura.inl", "VFX_ComposeCharacterAura"),
          "legacy CharacterAura implementation is removed");
    CHECK(Has(header, "VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC") &&
          Has(header, "VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_RISE") &&
          Has(header, "VFX_MESH_PARTICLE_VARIANT_SMOKE_LIGHT_RISE") &&
          Has(header, "VFX_MESH_PARTICLE_VARIANT_SMOKE_DARK_RISE") &&
          Has(header, "VFX_MESH_PARTICLE_VARIANT_FIRE_ROIL") &&
          Has(header, "VFX_MESH_PARTICLE_VARIANT_EMBER_SPARK_LIFT"),
          "public mesh-particle variants are complete");
    CHECK(Has(emitter, "ParticleDynamicsProfile") &&
          Has(emitter, "VFX_MeshParticleEmitter_SetTransform") &&
          Has(emitter, "s_meshParticleEmitters[handle].variant != variant") &&
          Has(emitter, "VFX_SurfaceRegistry_Get") && Has(emitter, "VFX_SURFACE_PLASMA_WISPS_NIAGARA") &&
          Has(emitter, "VFX_MESH_EMITTER_LIVE_MAX") &&
          Has(emitter, "return variant == VFX_MESH_PARTICLE_VARIANT_PLASMA_WISP_STATIC ? 36.0f : 16.0f") &&
          Has(emitter, "? 0.33f : 0.75f") &&
          !Has(emitter, "character/character_model.h"),
          "emitter is generic, transform-driven, and uses dynamics/profile data");
    CHECK(Has(emitter, "volumeSheet = 4") && Has(emitter, "normalTex") &&
          Has(emitter, "volumeSheet = 3"),
          "fire and smoke use packed decoders with companion normals");
    CHECK(Has(aura, "VFX_DrawMeshSurfaceAura") && Has(aura, "VFX_DrawModelSurfaceAura") &&
          Has(aura, "u_rimWidth") && Has(aura, "BeginShaderMode") &&
          !Has(aura, "SpawnParticle") &&
          !Has(aura, "character/character_model.h"),
          "surface aura is shader-only and generic");
    CHECK(Has(auraVs, "vertexNormal * 0.012"),
          "aura offsets its rim shell to avoid coplanar depth rejection");
    return failures ? 1 : 0;
}
