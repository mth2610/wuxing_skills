// Render primitives — low-level draw helpers shared across VFX_Compose*.
// Only vc_ground.inl depends on these.
//
// NOTE: the "energy field" crossed-plane technique (formerly VC_DrawEnergyField
// here) moved to core/ribbon_strip.h's DrawRibbonEnergyField — core/vfx_proc_ray.c's
// EnergyFlow needs it too, and core/ must not depend on composition/.

// Horizontal quad at the CURRENT matrix origin — caller manages push/translate/
// rotate for custom transforms.
static void VC_DrawGroundQuadXZ(Texture2D tex, float halfX, float halfZ, Color tint)
{
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-halfX, 0, -halfZ);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(halfX, 0, -halfZ);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(halfX, 0, halfZ);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-halfX, 0, halfZ);
    rlEnd();
    rlSetTexture(0);
}

// Rune/glow ring rotating around Y at pos — self-contained push/pop.
static void VC_DrawGroundRune(Texture2D tex, Vector3 pos, float radius, float angleDeg, Color tint)
{
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(angleDeg, 0, 1, 0);
    VC_DrawGroundQuadXZ(tex, radius, radius, tint);
    rlPopMatrix();
}
