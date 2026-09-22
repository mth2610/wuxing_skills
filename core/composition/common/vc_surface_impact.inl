/* Tester-facing composition wrapper for the reusable surface-impact event.
 * Gameplay should call VFX_SurfaceImpact_Emit with its collision result. */
void VFX_ComposeSurfaceImpact(Vector3 pos, VFX_ImpactSurface surface)
{
    VFX_SurfaceImpact_Emit(&(VFX_SurfaceImpactEvent){
        .position = pos, .normal = (Vector3){0.0f, 1.0f, 0.0f},
        .material = surface == VFX_IMPACT_SURFACE_METAL ? VC_MAT_METAL : VC_MAT_FIRE,
        .surface = surface, .scale = 1.25f, .severity01 = 0.85f
    });
}
