// Fluid Impact bench fixture. This deliberately calls the public event API
// directly so the tester isolates droplet collision/residue from the generic
// impact package's light, distortion and smoke beats.
void VFX_ComposeFluidImpact(Vector3 pos)
{
    FluidImpactEvent event = {
        .hitPoint = pos,
        .hitNormal = (Vector3){0.0f, 1.0f, 0.0f},
        .impulseDirection = Vector3Normalize((Vector3){0.35f, 1.0f, 0.20f}),
        .initialVelocity = (Vector3){1.4f, -6.5f, 0.8f},
        .force01 = 1.0f,
        .scale = 1.15f
    };
    FluidImpact_SpawnWater(&event);
}
