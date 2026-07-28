// Render primitives — low-level draw helpers shared across VFX_Compose*.
// Only vc_ground.inl depends on these.
//
// NOTE: the "energy field" crossed-plane technique (formerly VC_DrawEnergyField
// here) moved to core/ribbon_strip.h's DrawRibbonEnergyField — core/vfx_proc_ray.c's
// EnergyFlow needs it too, and core/ must not depend on composition/.

// Element → signature colour. Shared by the managed archetypes (proc_beam,
// ground_wave, orbital, aura_ring). Lived in vc_archetype.inl until that
// orchestrator was dissolved; it belongs here because vc_common.inl is the
// first thing common.inl pulls in, so every later .inl in the TU can see it.
static Color Arch_ElementColor(EffectPresetType e)
{
    switch (e)
    {
    case EFFECT_PRESET_WATER_SPLASH:
        return ELEMENT_COLOR_WATER;
    case EFFECT_PRESET_WOOD_BLOOM:
        return ELEMENT_COLOR_WOOD;
    case EFFECT_PRESET_FIRE_EXPLOSION:
        return ELEMENT_COLOR_FIRE;
    case EFFECT_PRESET_EARTH_CRACK:
        return ELEMENT_COLOR_EARTH;
    case EFFECT_PRESET_METAL_SHARD:
        return ELEMENT_COLOR_METAL;
    case EFFECT_PRESET_TAIJI_BURST:
        return ELEMENT_COLOR_TAIJI;
    default:
        return WHITE;
    }
}

// Linear RGB blend between two material colours. Alpha is set by the caller
// (VC_WithAlpha / ColorAlpha), so this deliberately returns 255 — a composition
// that mixed alpha here would fight its own envelope.
static Color VC_MixColor(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){(unsigned char)Math_Mix((float)a.r, (float)b.r, t),
                   (unsigned char)Math_Mix((float)a.g, (float)b.g, t),
                   (unsigned char)Math_Mix((float)a.b, (float)b.b, t),
                   255};
}

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
