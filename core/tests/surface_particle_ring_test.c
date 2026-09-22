// Headless contract — semantic surface particle ring stays generic and packed-data safe.
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)
static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb"); char text[65536]; size_t n;
    if (!f) return 0; n = fread(text, 1, sizeof(text) - 1, f); fclose(f); text[n] = '\0';
    return strstr(text, needle) != NULL;
}
int main(void)
{
    const char *src = "core/composition/common/vc_surface_particle_ring.inl";
    const char *api = "core/composition/visual_composer.h";
    CHECK(Has(api, "VFX_SURFACE_PARTICLE_RING_VARIANT_DUST") &&
          Has(api, "VFX_SURFACE_PARTICLE_RING_VARIANT_ENERGY_WISP") &&
          Has(api, "void VFX_ComposeSurfaceParticleRing"),
          "public generic surface-ring variants are declared");
    CHECK(Has(src, "VFX_SURFACE_SMOKE_PUFF_LIGHT_NIAGARA") &&
          Has(src, "VFX_SURFACE_SMOKE_PUFF_DARK_NIAGARA") &&
          Has(src, "VFX_SURFACE_SMOKE_WISPY_NIAGARA") &&
          Has(src, "VFX_SURFACE_PLASMA_WISPS_NIAGARA"),
          "dust, puff, wisp and energy use semantic surface profiles");
    CHECK(Has(src, "p.render.smokeSheet = 1;") && Has(src, "p.render.normalTex ="),
          "smoke and dust keep packed decoder plus paired normal map");
    CHECK(Has(src, "p.render.unlit = 1;") && Has(src, "VFX_BLEND_ADDITIVE"),
          "energy wisp is an emissive plasma path, not decoded smoke RGB");
    CHECK(Has(src, "initialRingRadius = Math_Mix(0.10f, 0.38f, severity01) * scale") &&
          Has(src, ".linearDragPerSecond = 1.8f") &&
          Has(src, "float speed = Math_Mix(3.4f, 5.8f, Random01()) * scale"),
          "ring begins tightly at R1 then reads its outward R2 expansion through physical impulse");
    CHECK(Has(src, "VFX_ComposeGroundDustRing") && Has(src, "VFX_SURFACE_PARTICLE_RING_VARIANT_DUST"),
          "old ground-dust name remains a dust compatibility wrapper");
    CHECK(Has("scripts/sync_vfx_test.py", "SURFACE PARTICLE RING") &&
          Has("sandbox/vfx_test.c", "s_surfaceParticleRingFixtureVariant"),
          "NEW FX fixture exposes deterministic variant cycling");
    return failures ? 1 : 0;
}
