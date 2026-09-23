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
    failed += !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_EARTH:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_FIRE:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_METAL:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_WOOD:") ||
              !Has("core/composition/common/vc_impact_dust.inl", "case VFX_IMPACT_SURFACE_WATER:");
    failed += !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeGroundDustRing(event->position, VC_MAT_EARTH,") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeDecalVariant(event->position, VC_MAT_ICE,") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeImpactDustVariant(event->position, VC_MAT_ICE,") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeContactSpark") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_IMPACT_DUST_VARIANT_DUST_PUFF") ||
              !Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeDecalVariant");
    failed += Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeFluidImpact") ||
              Has("core/composition/common/vc_impact_dust.inl", "VFX_ComposeIceCrystal");
    failed += !Has("core/composition/common/vc_surface_impact.inl", "case VFX_IMPACT_SURFACE_METAL: return VC_MAT_METAL;") ||
              !Has("core/composition/common/vc_surface_impact.inl", "case VFX_IMPACT_SURFACE_WOOD: return VC_MAT_WOOD;") ||
              !Has("core/composition/common/vc_surface_impact.inl", "case VFX_IMPACT_SURFACE_WATER: return VC_MAT_ICE;") ||
              !Has("core/composition/common/vc_surface_impact.inl", "default: return VC_MAT_EARTH;");
    printf("surface impact event: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
