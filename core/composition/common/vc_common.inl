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

// ── Shared by every SWEPT primary ───────────────────────────────────────────
//
// Both of these lived inside vc_swept_trail.inl until P1 needed them. They are
// here rather than copied because the copy is the failure mode this module keeps
// repeating: a composition grew its own history ring beside core/trail_system.h,
// then the trail system grew its own tube beside ProceduralMesh_BuildTubeAlongPath.
// Twelve lines of ramp is small enough to duplicate without noticing, which is
// exactly why it gets hoisted the first time a second caller appears.

// The element's tail→head ramp: its BODY colour through the cooling tail, its
// GLOW at the head. One flat colour along a swept thing is what makes it read as
// plastic, and it is a ColorGradient because that is what the trail system
// samples at segRatio.
#define VC_RAMP_MAX 16
static ColorGradient s_vcRamp[VC_RAMP_MAX];
static bool s_vcRampBuilt[VC_RAMP_MAX];

static const ColorGradient *VC_ElementRamp(VC_MaterialId mat)
{
    int i = (int)mat;
    if (i < 0 || i >= VC_RAMP_MAX)
        return NULL;
    if (!s_vcRampBuilt[i])
    {
        const VFX_ElementMaterial *m = VFX_Material(mat);
        ColorGradient_AddStop(&s_vcRamp[i], 0.00f, m->body);
        ColorGradient_AddStop(&s_vcRamp[i], 0.35f, m->body);
        ColorGradient_AddStop(&s_vcRamp[i], 1.00f, m->glow);
        s_vcRampBuilt[i] = true;
    }
    return &s_vcRamp[i];
}

// Two unit axes spanning the plane perpendicular to `unitNormal`, which MUST
// already be normalised.
//
// The guard is the whole reason this is a function. `cross(n, ref)` is ~zero
// when `ref` is parallel to `n`, and `Vector3Normalize` of ~zero returns garbage
// SILENTLY — no NaN, no log, just a frame that spans nothing, so every ring built
// on it collapses onto a line and the shape draws as a plane or vanishes
// (core/docs/LANDMINES.md, 30/07, which cost four rounds on the swept tube).
// Picking the reference away from `n` costs one comparison and removes the case
// entirely. Same guard `VC_DirCone` uses.
static void VC_PlaneFrame(Vector3 unitNormal, Vector3 *outA, Vector3 *outB)
{
    Vector3 ref = (fabsf(unitNormal.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                                : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 a = Vector3Normalize(Vector3CrossProduct(unitNormal, ref));
    *outA = a;
    *outB = Vector3CrossProduct(unitNormal, a);
}

// Tail memory in SECONDS → history nodes. The ceiling is TRAIL_HISTORY_COUNT
// (60), which at 60 Hz is exactly 1.0 s of history; asking for more silently
// gets 1.0 s. Four is the floor because a strip of three nodes has no shape.
static int VC_TrailNodesForLifetime(float lifetime, float sampleHz)
{
    int n = (int)(lifetime * sampleHz + 0.5f);
    if (n < 4)
        n = 4;
    if (n > TRAIL_HISTORY_COUNT)
        n = TRAIL_HISTORY_COUNT;
    return n;
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
