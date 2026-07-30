// ── PRIMARY. VFX_ComposeCoreGlow — one hot point of light ───────────────────
//
// EXTRACTED from VFX_ComposeChargeConverge, 29/07. The look is already signed
// off, so this costs no visual iteration — only its address changes. That is
// the whole reason extraction comes before invention (VFX_PLAN §Part 4): a
// primary invented from scratch costs three to five rounds of "write it blind,
// owner looks, guess again", and H1's swept trail took more than that.
//
// WHAT IT IS. A single bright point that reads as *too bright to have a colour*,
// with a falloff around it. Every composite that needs a destination or a source
// needs exactly this and nothing else: a charge's centre, an orb's heart, an
// impact's flash point, a beam's muzzle, a rune's hub. Before this it existed in
// one place, buried 80 lines inside a composite, and could not be fired on its
// own or judged on its own.
//
// WHY IT IS THREE SPRITES AND NOT ONE, and this is the part that is knowledge
// rather than taste:
//
//   1. THE HOT CORE — small, near-white, high emissiveBoost. Whitened at the
//      source because a saturated element hue stacks additively into more of the
//      same hue and never reaches white, so the boost has nothing to lift.
//   2. THE MID GLOW — wider, kept only just over the bloom threshold. The bright
//      pass CLAMPS each pixel's contribution (`bloom_max_energy` = 4.0), so past
//      that point raising the tiny core's boost adds nothing at all: **bloom
//      size is driven by how many pixels clear the threshold, not by how far one
//      pixel clears it.** This layer is the one that actually buys the bloom.
//   3. THE WIDE HALO — no boost. This is the glow AROUND the hot spot, not a
//      second hot spot, and it is what stops the core ending at a sprite edge.
//
// A single sprite cannot do all three: make it small and it has no falloff, make
// it big and additive brightness spreads over AREA so it gets DIMMER as it
// grows. That is also why the core's radius grows only gently with intensity.
//
// IMMEDIATE MODE. Call it every frame for as long as the glow should exist; it
// emits by RATE with a fractional accumulator, so its density does not move with
// the frame rate. It is not a one-shot and does not own a handle.

#define CORE_GLOW_RATE_MIN 8.0f  // sprites/sec at intensity 0
#define CORE_GLOW_RATE_MAX 30.0f // ...and at intensity 1
#define CORE_GLOW_BATCH_MAX 4    // clamp after a frame hitch

static SkillCurve s_coreGlowFade;
static bool s_coreGlowInit = false;

// x on the whole thing's size, and a kill switch for the point light. Both are
// look decisions, and the alternative to a tunable is a rebuild per guess.
static float s_coreGlowSize = 1.0f;
static float s_coreGlowLight = 1.0f;

static void CoreGlow_InitShared(void)
{
    if (s_coreGlowInit)
        return;
    // Hold, then fall. A linear fade on a sprite this bright reads as a blink;
    // the hold is what makes it a steady point rather than a flicker.
    FloatCurve_AddStop(&s_coreGlowFade, 0.00f, 1.00f);
    FloatCurve_AddStop(&s_coreGlowFade, 0.55f, 0.85f);
    FloatCurve_AddStop(&s_coreGlowFade, 1.00f, 0.00f);

    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("coreglow_size", &s_coreGlowSize, 1.0f);
    Tuning_RegisterFloat("coreglow_light", &s_coreGlowLight, 1.0f);
    s_coreGlowInit = true;
}

void VFX_ComposeCoreGlow(Vector3 center, VC_MaterialId mat, float radius,
                         float intensity01)
{
    CoreGlow_InitShared();
    if (radius <= 0.0f)
        radius = 1.0f;
    if (intensity01 < 0.0f)
        intensity01 = 0.0f;
    if (intensity01 > 1.0f)
        intensity01 = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float i01 = intensity01;
    float scale = radius * s_coreGlowSize;

    // A RATE carried between frames, never a count per call — the same rule as
    // any per-frame emitter (VFX_PLAN §0.3). A count here would make the glow's
    // brightness a function of the frame rate.
    static float s_accum = 0.0f;
    s_accum += GetFrameTime() * Math_Mix(CORE_GLOW_RATE_MIN, CORE_GLOW_RATE_MAX, i01);
    int batch = (int)s_accum;
    if (batch > CORE_GLOW_BATCH_MAX)
        batch = CORE_GLOW_BATCH_MAX;
    s_accum -= (float)batch;
    if (batch <= 0)
        return;

    for (int c = 0; c < batch; c++)
    {
        // 1. THE HOT CORE. Grows only gently: additive brightness is energy
        // spread over AREA, so a core that scales fast gets DIMMER as it fills.
        SpawnParticle((ParticleConfig){
            .position = center,
            .radius = (0.045f + 0.075f * i01) * scale,
            .lifetime = Math_Mix(0.10f, 0.18f, Random01()),
            // Nearly white — the one place the effect is too bright to have a
            // colour. The element hue returns only as it fades.
            .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.75f), 255),
            .colorEnd = VC_WithAlpha(m->glow, 0),
            .alphaCurve = &s_coreGlowFade,
            .render.blendMode = VFX_BLEND_ADDITIVE, // emits...
            .render.unlit = 1,                      // ...so no lighting multiply
            .render.emissiveBoost = Math_Mix(4.0f, 14.0f, i01),
        });
    }

    // 2 and 3 are the falloff AROUND the batch, not independent events, so they
    // fire once per batch rather than once per sprite. The batch itself is
    // rate-driven, so this stays framerate-independent — but it does mean their
    // density follows the core's rate, which is intended: a brighter core wants
    // a denser skirt.
    SpawnParticle((ParticleConfig){
        .position = center,
        .radius = (0.10f + 0.16f * i01) * scale,
        .lifetime = 0.13f,
        .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.5f), 200),
        .colorEnd = VC_WithAlpha(m->glow, 0),
        .alphaCurve = &s_coreGlowFade,
        .render.blendMode = VFX_BLEND_ADDITIVE,
        .render.unlit = 1,
        .render.emissiveBoost = Math_Mix(1.6f, 3.2f, i01),
    });

    SpawnParticle((ParticleConfig){
        .position = center,
        .radius = (0.22f + 0.30f * i01) * scale,
        .lifetime = 0.14f,
        .colorStart = VC_WithAlpha(m->soft, (unsigned char)(40 + 90 * i01)),
        .colorEnd = VC_WithAlpha(m->soft, 0),
        .alphaCurve = &s_coreGlowFade,
        .render.blendMode = VFX_BLEND_ADDITIVE,
        .render.unlit = 1,
        // NO boost. This is the glow around the hot spot; boosting it would make
        // it a second hot spot and the falloff would be gone.
    });

    // E2 point light, on its own timer. The pool is 16 slots and a 0.09 s light
    // spawned every frame at 60 fps would hold five of them for this one effect.
    if (s_coreGlowLight > 0.5f)
    {
        static float s_lightAccum = 0.0f;
        s_lightAccum += GetFrameTime();
        if (s_lightAccum >= 0.07f)
        {
            s_lightAccum = 0.0f;
            VFXLight_Spawn(center, m->soft, radius * Math_Mix(0.7f, 2.1f, i01),
                           0.09f, VFX_PRIORITY_LOW);
        }
    }
}
