// VFX_ComposeEnergyBurst — an energy explosion, and it is A SMOKE PUFF WITH A
// HOLE IN THE MIDDLE (owner, 28/07/2026). That sentence is the design:
//
//   PHASE 1, the shape. Sprites are launched centrifugally from a RING — a low
//     cylinder with no caps — so nothing fills the centre and the burst reads
//     as a shell opening rather than as a ball.
//   PHASE 2, the effect. The outward push is gone by the time they arrive (see
//     the force field: it is absent beyond `reach`), curl noise takes the
//     motion over, and the smoke churns and dissipates. This phase is the
//     explosion; phase 1 only puts the smoke where phase 2 can be seen.
//
// EVERY NUMBER THAT IS NOT ABOUT THE HOLE COMES FROM `vc_smoke_puff.inl`.
// The first version invented its own: sprites a fifth the size, ten times the
// speed, and a growth ramp of 0.14 -> 2.40. It produced a necklace of separate
// dots, and the overlap was then chased with a formula deriving sprite radius
// from ring circumference — correct arithmetic answering the wrong question.
// SmokePuff's sprites are 0.20-1.06 m across and overlap without any of it.
// When a working effect is the reference, copy its numbers before deriving new
// ones.
//
// WHY NOT ONE BIG FLIPBOOK, the other candidate: a single baked sheet of the
// whole explosion is one fixed silhouette, locked to one scale, ~16 MB, blind
// to the scene. Shipping games do bake hero explosions that way; the repeatable
// gameplay ones are particle systems like this, where the sheet describes a
// small ELEMENT and the system describes the event.

#define ENERGY_BURST_FIELDS 6      // one per concurrent burst; oldest is reused

static ForceField s_ebFld[ENERGY_BURST_FIELDS];
static int        s_ebFldNext = 0;
// The burst's OWN animation templates rather than SmokePuff's. Two reasons:
// the rate must be slow enough that the longest life PLUS the largest phase
// offset still lands inside the 64 frames (run past the end and the sheet's
// empty tail makes the sprite vanish while its alpha says visible — the E4
// landmine), and a wider spread of rates pulls the sprites apart faster.
//   slowest: 64/2.4 * 0.78 = 20.8 fps -> (1.7 s life + 0.6 s phase) = 48 frames
//   fastest: 64/2.4 * 1.00 = 26.7 fps -> 61 frames. Both inside the sheet.
#define ENERGY_BURST_RATES 4
#define ENERGY_BURST_PHASE_MAX 0.60f
static SpriteAnim s_ebAnim[ENERGY_BURST_RATES];
static bool       s_ebAnimReady = false;
static SkillCurve s_ebGrow = {0};
static SkillCurve s_ebFade = {0};
static bool       s_ebInit = false;

static void EnergyBurst_InitShared(void)
{
    if (s_ebInit)
        return;

    // SmokePuff's growth: a puff sprite is already large at birth and swells
    // modestly. It is not a dot that balloons.
    FloatCurve_AddStop(&s_ebGrow, 0.0f, 0.90f);
    FloatCurve_AddStop(&s_ebGrow, 0.30f, 1.08f);
    FloatCurve_AddStop(&s_ebGrow, 1.0f, 1.40f);

    // Holds through the chaotic phase, then goes. A curve that is already down
    // to 0.3 by mid-life puts the churn — the part that IS the explosion —
    // behind a fade that has finished.
    FloatCurve_AddStop(&s_ebFade, 0.0f, 0.85f);
    FloatCurve_AddStop(&s_ebFade, 0.15f, 1.0f);
    FloatCurve_AddStop(&s_ebFade, 0.55f, 0.80f);
    FloatCurve_AddStop(&s_ebFade, 0.80f, 0.35f);
    FloatCurve_AddStop(&s_ebFade, 1.0f, 0.0f);

    s_ebInit = true;
}

static void EnergyBurst_InitAnim(void)
{
    if (s_ebAnimReady || !s_smokeFbReady)
        return;
    static const float rateMul[ENERGY_BURST_RATES] = {1.0f, 0.92f, 0.85f, 0.78f};
    for (int r = 0; r < ENERGY_BURST_RATES; r++)
        SpriteAnim_Init(&s_ebAnim[r], 8, 8, 64, (64.0f / 2.4f) * rateMul[r],
                        ANIM_ONCE);
    s_ebAnimReady = true;
}

// One field per burst, because a per-particle field is not something the pool
// has. Slots recycle after ENERGY_BURST_FIELDS bursts.
//
// THE MOTION MODEL, corrected by the owner (28/07/2026): a fairly strong
// centrifugal IMPULSE at birth that decays to zero, plus a little noise for
// naturalness — NOT a strong curl. The previous version had curl at 5.0-9.5
// and it threw the sprites around after the ring formed, which is motion the
// explosion does not have; an explosion's gas coasts and stops.
//
// The impulse is now the particles' INITIAL VELOCITY rather than a repulsor
// field. "Strong, then decaying to zero" is exactly what a launch velocity
// against drag does, and it needs no field at all: with drag `k`, speed decays
// as exp(-k*t), so at k = 4.5 the outward motion is down to 1% of its launch
// value in about a second — inside the sprite's life, so the ring visibly
// settles rather than drifting off screen.
static ForceField *EnergyBurst_Field(float curl, float drag)
{
    ForceField *ff = &s_ebFld[s_ebFldNext];
    s_ebFldNext = (s_ebFldNext + 1) % ENERGY_BURST_FIELDS;
    ForceField_Clear(ff);

    // The brake that turns the impulse into a settling ring.
    ForceField_AddLayer(ff, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = drag,
    });
    // "Một xíu nhiễu" — enough that the ring is not a machined circle, not
    // enough to be a force in its own right. One scale, coarse: fine noise on
    // a sprite this large is invisible anyway, since the sheet's own billows
    // are already the small-scale detail.
    ForceField_AddLayer(ff, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = curl,
        .noiseScale = 0.7f,
        .noiseSpeed = 0.6f,
    });
    return ff;
}

// `intensity` 0..1 scales count, speed and brightness together.
void VFX_ComposeEnergyBurst(Vector3 pos, VC_MaterialId matId, float scale,
                            float intensity)
{
    EnergyBurst_InitShared();
    SmokePuff_InitShared();          // the smoke sheet is loaded there
    EnergyBurst_InitAnim();
    // ALWAYS announce the path, once. "The sprites hold one frame from birth to
    // death" has two possible causes that look identical on screen — the sheet
    // failed to load (static sprites, no animation at all) or it loaded and the
    // rate is wrong — and only a log line can tell them apart.
    {
        static bool announced = false;
        if (!announced)
        {
            announced = true;
            if (s_ebAnimReady)
                TraceLog(LOG_INFO, "EnergyBurst: flipbook ON — tex %u, %d frames "
                                   "at %.1f-%.1f fps, phase 0-%.2f s",
                         (unsigned)s_smokeFbTex.id, s_ebAnim[0].frameCount,
                         s_ebAnim[ENERGY_BURST_RATES - 1].fps, s_ebAnim[0].fps,
                         ENERGY_BURST_PHASE_MAX);
            else
                TraceLog(LOG_WARNING, "EnergyBurst: flipbook OFF (smoke sheet not "
                                      "loaded) — static sprites, so every sprite "
                                      "holds ONE frame for its whole life.");
        }
    }

    const float ity = Clamp(intensity, 0.0f, 1.0f);
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color body = mat ? mat->body : (Color){210, 70, 45, 255};
    Color glow = mat ? mat->glow : (Color){255, 120, 90, 255};

    ForceField *ff = EnergyBurst_Field(Math_Mix(0.6f, 1.3f, ity),   // a little noise
                                       4.5f);                       // drag

    // SmokePuff spreads 28 across a DISC; a ring of the same radius has to
    // cover a circumference with them instead of an area, so it needs more to
    // reach the same density — and the burst wants to read as a continuous
    // mass, not as countable puffs. Fill rate is the ceiling here: the atlas
    // cross-fade draws every body TWICE and one quarter also owns an accent,
    // so this is the first number to pull back if the frame rate drops.
    int count = (int)(60.0f * Math_Mix(0.75f, 1.3f, ity) * Clamp(scale, 0.3f, 2.0f));
    if (count < 6)
        count = 6;

    for (int i = 0; i < count; i++)
    {
        // THE HOLE. SmokePuff spawns across a disc with a sqrt distribution for
        // an even area spread; this spawns on a RING instead. That single
        // change is the whole structural difference between the two effects.
        float ang = Random01() * 2.0f * PI;
        float rad = Math_Mix(0.70f, 1.0f, Random01()) * 0.42f * scale;
        Vector3 dir = {cosf(ang), 0.0f, sinf(ang)};
        Vector3 p = {pos.x + dir.x * rad,
                     pos.y + (Random01() - 0.5f) * 0.20f * scale,
                     pos.z + dir.z * rad};

        // THE IMPULSE. Strong enough at birth to read as a detonation, and it
        // is spent by drag rather than by any force turning it around: speed
        // decays as exp(-4.5*t), so ~0.35 s in it is a third of launch and by
        // ~0.8 s it has stopped. The ring's final radius is therefore
        // launch/drag ~ 4/4.5 ~ 0.9 m at scale 1 — set the SPEED to move the
        // ring, not a separate radius.
        float outward = Math_Mix(3.0f, 5.5f, Random01()) * scale * Math_Mix(0.75f, 1.0f, ity);
        Vector3 tangent = {-dir.z, 0.0f, dir.x};
        Vector3 vel = Vector3Add(Vector3Scale(dir, outward),
                                 Vector3Scale(tangent, (Random01() - 0.5f) * 0.7f * scale));
        vel.y += (Random01() - 0.5f) * 0.45f * scale;

        // SmokePuff's 1.1-2.0, trimmed: a burst clears faster than a drifting
        // puff, but it still has to outlast the flight to the ring or the
        // chaotic phase never happens on screen.
        float life = Math_Mix(1.0f, 1.7f, Random01());

        // Semantic BODY. This is pigment/energy density, not light: it alpha-
        // occludes through the shared VFXBody target, which is what keeps the
        // element hue legible over a bright floor or sky. The former single
        // additive population could only add destination light and therefore
        // converged toward white by blend law.
        ParticleConfig bodyParticle = {
            .position = p,
            .velocity = vel,
            // SmokePuff's exact size law: mostly small with a long tail of
            // large ones (the pow 1.8 weighting), times the flipbook size
            // multiplier it uses because one simulated puff must be read at
            // puff size. This is what makes neighbours overlap at all.
            .radius = Math_Mix(0.14f, 0.14f + 0.42f * 1.4f,
                               powf(Random01(), 1.8f)) * scale * 1.45f,
            .lifetime = life,
            .colorStart = VC_WithAlpha(body, (unsigned char)(255.0f * 0.16f
                              * Math_Mix(0.70f, 1.25f, Random01())
                              * Math_Mix(0.7f, 1.0f, ity))),
            .colorEnd = VC_WithAlpha(body, 0),
            .forceField = ff,
            .radiusCurve = &s_ebGrow,
            .alphaCurve = &s_ebFade,
            .render.blendMode = VFX_BLEND_ALPHA,
            // Energy density is self-coloured but does not radiate HDR. Keeping
            // it unlit avoids a night environment multiplying cyan/orange into
            // brown while the separate accent population below owns radiance.
            .render.unlit = 1,
            .render.emissiveBoost = 1.0f,
            .render.contrastProfile = VFX_CONTRAST_ENERGY,
            // The smoke sheet.
            .render.texture = s_smokeFbReady ? s_smokeFbTex
                                             : s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .spriteAnim = s_ebAnimReady ? &s_ebAnim[i % ENERGY_BURST_RATES] : NULL,
            // THE FIX for "it looks like one image moving". SpriteAnim derives
            // its frame from ABSOLUTE age, so sprites born in the same instant
            // hold the same frame for their whole lives — a rate table only
            // splits the burst into as many identical groups as it has rates.
            // A random phase gives every sprite a different point in the sheet.
            .spriteAnimPhase = Random01() * ENERGY_BURST_PHASE_MAX,
            // A random angle at BIRTH, and no spin after it. The angle is what
            // stops one sheet read sixty times from showing sixty copies of the
            // same silhouette; continuous rotation on top of a billow that is
            // already rolling is what reads as churning (SmokePuff records the
            // same, and turns its flipbook spin down to 0.12x for it).
            .rotation = Random01() * 2.0f * PI,
        };
        SpawnParticle(bodyParticle);

        // Semantic ACCENT/EMISSION. Only one quarter of the body lobes receives
        // a smaller aligned hot core; the two populations share position,
        // motion, animation phase and rotation, so this reads as radiance from
        // matter rather than a second unrelated cloud. Low coverage prevents
        // additive overlap from flattening the entire annulus.
        if ((i & 3) == 0)
        {
            ParticleConfig accentParticle = bodyParticle;
            accentParticle.radius *= 0.58f;
            accentParticle.colorStart = VC_WithAlpha(
                glow, (unsigned char)(255.0f * 0.18f * Math_Mix(0.75f, 1.0f, ity)));
            accentParticle.colorEnd = VC_WithAlpha(glow, 0);
            accentParticle.render.blendMode = VFX_BLEND_ADDITIVE;
            accentParticle.render.emissiveBoost = Math_Mix(1.25f, 1.65f, ity);
            SpawnParticle(accentParticle);
        }
    }

    // NO separate core flash: a cluster of motionless bright sprites at the
    // origin is not part of this model, and a flash belongs to the LIGHT beat,
    // which costs no overdraw and reads brighter than any sprite can.
}
