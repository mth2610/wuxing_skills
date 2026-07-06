// Persistent AoE Zone archetype — a ground-hugging field that lives for the
// skill's whole active duration (lava pool, frost field, poison cloud...),
// unlike the one-shot GROUND_* patterns in vc_ground.inl. Call every frame
// while the zone is active; ambient particles are probability-gated per
// call (not a fixed count) since this isn't a single burst.

static ForceField s_zoneDriftFld[5]; // one per ZoneStyle, lazy-built
static bool s_zoneFldInit[5] = {0};

static ForceField *ZoneGetDriftField(ZoneStyle style)
{
    ForceField *f = &s_zoneDriftFld[style];
    if (!s_zoneFldInit[style])
    {
        ForceField_Clear(f);
        ForceField_AddLayer(f, (ForceLayer){.type = FORCE_NOISE_CURL, .strength = 0.25f, .noiseScale = 0.35f, .noiseSpeed = 0.5f});
        s_zoneFldInit[style] = true;
    }
    return f;
}

void VFX_ComposeZone(ZoneStyle style, Vector3 pos, float radius, float progress, float time)
{
    Color color;
    GroundPatternStyle groundStyle;
    switch (style)
    {
        case ZONE_LAVA:   color = VFX_Material(VC_MAT_FIRE)->glow;   groundStyle = GROUND_LAVA;  break;
        case ZONE_FROST:  color = VFX_Material(VC_MAT_ICE)->glow;    groundStyle = GROUND_FROST; break;
        case ZONE_POISON: color = VFX_Material(VC_MAT_POISON)->body; groundStyle = GROUND_CRACK_RADIAL; break;
        case ZONE_HOLY:   color = VFX_Material(VC_MAT_HOLY)->glow;   groundStyle = GROUND_MAGIC_CIRCLE; break;
        case ZONE_VOID:   color = VFX_Material(VC_MAT_VOID)->glow;   groundStyle = GROUND_RUNE; break;
        default:          color = VFX_Material(VC_MAT_TAIJI)->body;  groundStyle = GROUND_MAGIC_CIRCLE; break;
    }

    // Ground field never fully "completes" — keep progress in the pattern's
    // grow-then-hold band so it reads as an ongoing zone, not a burst.
    VFX_GroundPattern(groundStyle, pos, radius, fminf(progress, 0.6f), time);

    // Fade-in/out size so rising motes never pop into or out of existence.
    static SkillCurve s_zoneMoteSize = {0};
    static bool s_zoneCurveInit = false;
    if (!s_zoneCurveInit)
    {
        FloatCurve_AddStop(&s_zoneMoteSize, 0.0f, 0.0f);
        FloatCurve_AddStop(&s_zoneMoteSize, 0.25f, 1.0f);
        FloatCurve_AddStop(&s_zoneMoteSize, 0.75f, 0.85f);
        FloatCurve_AddStop(&s_zoneMoteSize, 1.0f, 0.0f);
        s_zoneCurveInit = true;
    }

    // Ambient particles: gated by probability per call, not a burst count —
    // callers invoke this once per frame while the zone is active.
    if (GetRandomValue(0, 100) < 25)
    {
        Vector3 spawnPos = VC_RingPointXZ(pos, radius * Random01(), Random01() * 2.0f * PI);
        spawnPos.y += 0.02f;

        Vector3 vel = (style == ZONE_FROST) ? (Vector3){0.0f, 0.05f, 0.0f}
                    : (style == ZONE_VOID)  ? (Vector3){0.0f, 0.12f, 0.0f}
                                             : (Vector3){0.0f, 0.2f + Random01() * 0.2f, 0.0f};

        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = vel,
            .colorStart = color,
            .colorEnd = ColorAlpha(color, 0.0f),
            .radius = 0.03f + Random01() * 0.03f,
            .lifetime = 0.8f + Random01() * 0.6f,
            .radiusCurve = &s_zoneMoteSize,
            .forceField = ZoneGetDriftField(style)});
    }

    // Mist blanket — big, slow, dim alpha puffs hugging the floor. This is
    // the layer that makes the zone read as a volume you stand IN, not a
    // painted floor sticker. Desaturated so it doesn't compete with motes.
    if (GetRandomValue(0, 100) < 12)
    {
        Vector3 mistPos = VC_RingPointXZ(pos, radius * (0.3f + 0.7f * Random01()), Random01() * 2.0f * PI);
        mistPos.y += 0.06f;
        Color mist = {(unsigned char)(color.r / 3 + 55), (unsigned char)(color.g / 3 + 55),
                      (unsigned char)(color.b / 3 + 55), 70};
        SpawnParticle((ParticleConfig){
            .position = mistPos,
            .velocity = (Vector3){(Random01() - 0.5f) * 0.08f, 0.02f + Random01() * 0.03f, (Random01() - 0.5f) * 0.08f},
            .colorStart = mist,
            .colorEnd = ColorAlpha(mist, 0.0f),
            .radius = 0.12f + Random01() * 0.1f,
            .lifetime = 1.8f + Random01() * 1.2f,
            .radiusCurve = &s_zoneMoteSize,
            .forceField = ZoneGetDriftField(style)});
    }

    // Occasional burst — a rare, localized event (lava bubble popping, ice
    // crackle, poison bubble...) that breaks the ambient loop's monotony.
    if (GetRandomValue(0, 1000) < 12)
    {
        Vector3 burstPos = VC_RingPointXZ(pos, radius * Random01() * 0.8f, Random01() * 2.0f * PI);
        burstPos.y += 0.05f;
        VFX_ComposeGlintBurst(burstPos, 5, 0.1f, color);
        VFXLight_Spawn(burstPos, color, 0.8f, 0.25f, VFX_PRIORITY_LOW);
    }

    // Heat distortion for the hot/void zones — the air above the field warps.
    if ((style == ZONE_LAVA || style == ZONE_VOID) && GetRandomValue(0, 100) < 3)
        ScreenDistort_Add(Vector3Add(pos, (Vector3){0, 0.3f, 0}), radius * 0.8f, 0.07f, 1.0f, 0.9f);

    // Low-frequency ambient light so the zone reads even without particles
    // in frame — cheap flicker via time-based sine, not a real light spawn
    // every frame (VFXLight pool is small and shared project-wide).
    if (GetRandomValue(0, 100) < 10)
        VFXLight_Spawn(pos, color, radius * 0.6f, 0.3f, VFX_PRIORITY_LOW);
}
