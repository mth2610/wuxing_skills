/* Tester-facing composition wrapper for the reusable surface-impact event.
 * Gameplay should call VFX_SurfaceImpact_Emit with its collision result. */
static VC_MaterialId SurfaceImpactFixtureMaterial(VFX_ImpactSurface surface)
{
    switch (surface) {
    case VFX_IMPACT_SURFACE_METAL: return VC_MAT_METAL;
    case VFX_IMPACT_SURFACE_WOOD: return VC_MAT_WOOD;
    case VFX_IMPACT_SURFACE_WATER: return VC_MAT_ICE;
    case VFX_IMPACT_SURFACE_FIRE: return VC_MAT_FIRE;
    case VFX_IMPACT_SURFACE_EARTH:
    default: return VC_MAT_EARTH;
    }
}

void VFX_ComposeSurfaceImpact(Vector3 pos, VFX_ImpactSurface surface)
{
    VFX_SurfaceImpact_Emit(&(VFX_SurfaceImpactEvent){
        .position = pos, .normal = (Vector3){0.0f, 1.0f, 0.0f},
        .material = SurfaceImpactFixtureMaterial(surface),
        .surface = surface, .scale = 1.25f, .severity01 = 0.85f
    });
}
