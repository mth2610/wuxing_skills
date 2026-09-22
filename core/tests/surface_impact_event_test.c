/* Contract: one caller-supplied receiver selects one coherent existing
 * response family. This is a source-level test because the compositions own
 * renderer-backed pools and are not headless constructs. */
#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb"); static char text[65536]; size_t n;
    if (!f) return 0;
    n = fread(text, 1, sizeof(text) - 1, f); fclose(f); text[n] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    int failed = 0;
    failed += !Has("core/composition/visual_composer.h", "VFX_ImpactSurface") ||
              !Has("core/composition/visual_composer.h", "VFX_SurfaceImpact_Emit");
    failed += !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_GROUND:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_STONE:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_METAL:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_WOOD:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_WATER:");
    failed += !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeFluidImpact(event->position)") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeContactSpark") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_IMPACT_DUST_VARIANT_DARK_SMOKE_PUFF") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_IMPACT_DUST_VARIANT_DUST_PUFF") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeDecalVariant");
    printf("surface impact event: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
