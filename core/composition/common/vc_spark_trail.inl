// ── PRIMARY: VFX_ComposeSparkTrail — one small thing with a curved tail ─────
//
// The owner's diagnosis, 29/07/2026, and it is the right one: Charge Converge
// and the deleted Spirit Swarm never looked good because their motes are DOTS.
// Energy being pulled into a point reads as energy only if each mote drags a
// tail behind it, and neither of them had one to reach for.
//
// WHY NOT JUST STRETCH THE PARTICLE. `stretchStrength` (particle_system.c:1003)
// already streaks a sprite along its velocity, it is free, and it is the right
// answer for sparks thrown in a straight line — VFX_ComposeSweepSlash uses it
// for exactly that. It cannot do this job: a stretched sprite is a STRAIGHT
// segment, and a mote spiralling into a point is doing nothing but turning. The
// stretch would point at the tangent and the curve — the whole read of "being
// drawn in" — would be gone. That is VFX_PLAN §0.1.4 in miniature: re-emitting
// along a path approximates it and loses the moment it turns hard.
//
// So this is `TRAIL_TYPE_WISP`, which keeps real node history and is another of
// the trail system's entry points that had no consumer at all. One call = one
// spark with a curved tail that self-terminates; the caller spawns them at a
// RATE and does nothing else.
//
// A TRUE PRIMARY, in the Part 4 sense: the smallest thing with a name, no
// timing beyond its own life, callable alone, benched alone. Charge, swarm,
// impact sparks and projectile motes are all "spawn these at a rate", which is
// the caller's business and not this file's.

#define SPARK_TRAIL_NODES 12      // history nodes per spark — a tail, not a rope

// Aspect: a comet/wisp is ~1:14 against its OWN length (core/docs/LANDMINES.md,
// "Thickness is a ratio against the thing's OWN length"). The trail system's
// WISP draw treats `thick` as a HALF-width, so half of 1/14 is 1/28.
#define SPARK_TRAIL_ASPECT (1.0f / 28.0f)

static SkillCurve s_sparkWidth = {0};
static SkillCurve s_sparkAlpha = {0};
static bool       s_sparkInit  = false;
static float      s_sparkWidthMul = 1.0f;
static float      s_sparkLifeMul  = 1.0f;

static void SparkTrail_InitShared(void)
{
    if (s_sparkInit) return;

    // BOTH ENDS COME TO A POINT. The WISP type's built-in taper
    // (ComputeWispStyleTaper) is pointed at the tail and FLAT at the head — it
    // reaches full width by segRatio 0.5 and stays there — so a spark drawn with
    // it ends in a cut-off rectangle at the very place the eye is looking. A
    // lens is the shape (core/docs/LANDMINES.md): widest just behind the head,
    // needle at both tips.
    FloatCurve_AddStop(&s_sparkWidth, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sparkWidth, 0.30f, 0.62f);
    FloatCurve_AddStop(&s_sparkWidth, 0.80f, 1.00f);
    FloatCurve_AddStop(&s_sparkWidth, 1.00f, 0.20f);

    // Brightness rides toward the head: the tail is what is LEFT of the light.
    // It must fall at least as fast as the width does, or the last stretch is
    // sub-pixel while still visible and breaks into dashes (LANDMINES, 29/07).
    FloatCurve_AddStop(&s_sparkAlpha, 0.00f, 0.00f);
    FloatCurve_AddStop(&s_sparkAlpha, 0.30f, 0.45f);
    FloatCurve_AddStop(&s_sparkAlpha, 0.85f, 1.00f);
    FloatCurve_AddStop(&s_sparkAlpha, 1.00f, 1.00f);

    Tuning_RegisterFloat("spark_trail_width", &s_sparkWidthMul, 1.0f);
    Tuning_RegisterFloat("spark_trail_life",  &s_sparkLifeMul,  1.0f);
    s_sparkInit = true;
}

// `vel` is the spark's velocity in m/s — it sets both the motion and the
// direction the tail is laid in. `length` is the tail in METRES; `life` in
// seconds. Returns the trail id, or -1 when the pool is full (500 entities), so
// a caller emitting at a rate can simply ignore the result.
int VFX_ComposeSparkTrail(Vector3 pos, Vector3 vel, VC_MaterialId matId,
                          float length, float life)
{
    SparkTrail_InitShared();
    if (length <= 0.0f) length = 0.35f;
    if (life   <= 0.0f) life   = 0.5f;
    life   *= s_sparkLifeMul;

    const VFX_ElementMaterial *m = VFX_Material(matId);

    // The strand is laid BACKWARD from the head. SpawnTrailEntity builds a WISP
    // as history[h] = pos + strandDir * u * len, and the WISP draw maps
    // segRatio = 1 to history[0] (trail_system.c:113) — i.e. index 0 is the
    // HEAD. So `target` must point where the tail should trail, which is
    // against the velocity, not along it.
    Vector3 back = (Vector3LengthSqr(vel) > 1e-6f)
                       ? Vector3Scale(Vector3Normalize(vel), -1.0f)
                       : (Vector3){0.0f, -1.0f, 0.0f};

    TrailConfig cfg = {0};
    cfg.type        = TRAIL_TYPE_WISP;
    cfg.pos         = pos;
    cfg.vel         = vel;
    cfg.target      = back;                 // strand direction, NOT a destination
    cfg.len         = length;
    cfg.thick       = length * SPARK_TRAIL_ASPECT * s_sparkWidthMul;
    cfg.trailLength = (float)SPARK_TRAIL_NODES;
    cfg.life        = life;
    // Hot identity colour, whitened a little: a spark is the emissive highlight
    // of the element, not its body.
    cfg.tint        = VC_WithAlpha(VC_Whiten(m->glow, 0.30f), 235);
    // The element's own field, so a fire spark rises and a water one falls
    // without this file knowing which is which (VFX_PLAN §0.3).
    cfg.forceField  = m->fld;
    cfg.widthCurve  = &s_sparkWidth;
    cfg.alphaCurve  = &s_sparkAlpha;
    cfg.smoothSpline = true;
    // The blend law: a spark EMITS, so additive and never through the lighting
    // multiply. Camera-facing, and no inner core — the two settings shared by
    // every trail style in this engine that does NOT break into dashes
    // (ENGINE_LANDMINES, "A plane-pinned ribbon dashes where it CURVES"; a
    // thread this thin cannot afford a second, thinner, brighter strip on top).
    cfg.blendMode        = BLEND_ADDITIVE;
    cfg.ribbonMode       = RIBBON_CAMERA_FACING;
    cfg.disableInnerCore = true;
    // LOW: a spark must never evict an ultimate's trail. The pool recycles the
    // lowest priority when full, which for an emitter running at a rate is
    // exactly the behaviour wanted — the oldest spark loses.
    cfg.priority    = VFX_PRIORITY_LOW;

    return SpawnTrailEntity(cfg);
}
