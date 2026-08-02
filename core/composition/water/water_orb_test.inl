/* SSF-only water projectile fixture: no PBD is requested at flight or impact. */
void VFX_ComposeWaterOrb(Vector3 start, Vector3 target)
{
    const VFX_ElementMaterial *water = VFX_Material(VC_MAT_WATER);
    FluidWaterOrbEvent event = {.start = start, .target = target, .hitNormal = {0.0f, 1.0f, 0.0f},
        .travelTime = 0.72f, .radius = 0.44f, .force01 = 0.82f,
        .bodyColor = water->body, .glowColor = water->glow, .softColor = water->soft};
    FluidWaterOrb_Spawn(&event);
}
