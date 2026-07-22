// flame_volume.inl — Đợt E / F3. Fire rebuilt on the F1 lit-particle foundation
// and on F2's smoke, replacing the fire_funnel approach.
//
// What was wrong with the old one (ELDEN_VFX_SPEC.md §0.1b, fire_funnel.fs:69-76):
//
//   1. The colour ramp was three smoothstep mixes red -> orange -> white. Not a
//      black-body curve, so it never read as combustion — just a hot gradient.
//   2. NO transition to smoke. Real flame darkens, desaturates and turns to
//      smoke where it cools, and that hand-off is most of what makes fire read
//      as fire rather than as glowing gas. This was the single biggest miss.
//   3. One additive draw for the whole flame, so it could never be darker than
//      its background — the exact reason it read as gas (F1b, the blend law).
//   4. Constant-velocity rise, which reads as a jet. Fire accelerates while it
//      is hot and decelerates as it cools.
//
// The structure here is three populations that are ONE system, sharing an age
// axis: a small additive CORE (the hottest, brightest part), an alpha BODY that
// carries the black-body ramp down to dark, and SMOKE that the body spawns as it
// dies. Nothing is stacked after the fact — the smoke is literally what the
// flame becomes.

// Prefixed s_fvol* / FVOL_* rather than s_flame* / FLAME_*: every .inl in this
// module is pasted into ONE translation unit, so a plain name like s_flameFld
// collides with the identically-named static already in fire.inl. Pick a prefix
// unique to the file, not to the concept.
#define FVOL_MAX_CORE 10
#define FVOL_MAX_BODY 22

static ColorGradient s_fvolCoreGrad = {0};   // additive hot core
static ColorGradient s_fvolBodyGrad = {0};   // black-body body, ends dark
static SkillCurve    s_fvolRise     = {0};
static SkillCurve    s_fvolFade     = {0};
static SkillCurve    s_fvolGrow     = {0};
static ForceField    s_fvolFld      = {0};
static ParticleConfig s_fvolSmokeSeed;       // what a dying body ember becomes
static bool s_fvolInit = false;

static float s_fvolCoreAlpha = 0.55f;
static float s_fvolSmokeAmt  = 1.0f;
// Rise speed, as a multiplier on everything vertical. Meter-scale is unforgiving
// here: a 1 m flame whose particles travel 1-2 m in their 0.5 s lifetime reads as
// a blowtorch, not as fire — the embers outrun the flame that made them. Visible
// rise for a flame this size is more like 0.3-0.5 m over a particle's life.
static float s_fvolRiseMul   = 1.0f;
// Base width. A flame is TALL and narrow; the ratio of base radius to rise
// height is what decides whether it reads as a flame or as a fireball. A wide
// base with a short rise blurs into a ball no matter how good the colour ramp
// is — and enlarging the particles to fill it makes that worse, not better.
static float s_fvolWidthMul  = 1.0f;

static void FVol_InitShared(void)
{
    if (s_fvolInit)
        return;

    Tuning_RegisterFloat("flame_core_alpha", &s_fvolCoreAlpha, 0.55f);
    Tuning_RegisterFloat("flame_smoke_amount", &s_fvolSmokeAmt, 1.0f);
    Tuning_RegisterFloat("flame_rise_mul", &s_fvolRiseMul, 1.0f);
    Tuning_RegisterFloat("flame_width_mul", &s_fvolWidthMul, 1.0f);

    // BLACK-BODY ramp, not three smoothsteps. Weighted so the flame spends most
    // of its life in the orange band and reaches white only at the very hottest
    // instant — an even spread across the ramp is what makes stylised fire look
    // like a gradient swatch instead of like burning.
    ColorGradient_AddStop(&s_fvolCoreGrad, 0.00f, (Color){255, 252, 236, 255}); // white-hot
    ColorGradient_AddStop(&s_fvolCoreGrad, 0.12f, (Color){255, 232, 150, 255}); // yellow
    ColorGradient_AddStop(&s_fvolCoreGrad, 0.45f, (Color){255, 158, 48, 220});  // orange
    ColorGradient_AddStop(&s_fvolCoreGrad, 0.80f, (Color){214, 74, 18, 90});    // red
    ColorGradient_AddStop(&s_fvolCoreGrad, 1.00f, (Color){96, 26, 8, 0});

    // The body carries the SAME ramp but continues past red into dark grey —
    // this tail is the cooling, and it is what lets the flame meet its smoke
    // without a visible seam.
    ColorGradient_AddStop(&s_fvolBodyGrad, 0.00f, (Color){255, 216, 130, 235});
    ColorGradient_AddStop(&s_fvolBodyGrad, 0.28f, (Color){248, 140, 40, 210});
    ColorGradient_AddStop(&s_fvolBodyGrad, 0.55f, (Color){186, 62, 18, 150});
    ColorGradient_AddStop(&s_fvolBodyGrad, 0.78f, (Color){92, 46, 34, 90});     // cooling
    ColorGradient_AddStop(&s_fvolBodyGrad, 1.00f, (Color){44, 40, 38, 0});      // meets smoke

    // Buoyancy: fast while hot, slowing as it cools. Constant rise reads as a
    // jet; this is what gives the licking, unsteady motion.
    FloatCurve_AddStop(&s_fvolRise, 0.0f, 1.35f);
    FloatCurve_AddStop(&s_fvolRise, 0.35f, 1.0f);
    FloatCurve_AddStop(&s_fvolRise, 1.0f, 0.35f);

    FloatCurve_AddStop(&s_fvolFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_fvolFade, 0.08f, 1.0f);
    FloatCurve_AddStop(&s_fvolFade, 0.6f, 0.8f);
    FloatCurve_AddStop(&s_fvolFade, 1.0f, 0.0f);

    // Narrows as it rises — a flame tapers, unlike smoke which only expands.
    FloatCurve_AddStop(&s_fvolGrow, 0.0f, 0.7f);
    FloatCurve_AddStop(&s_fvolGrow, 0.3f, 1.0f);
    FloatCurve_AddStop(&s_fvolGrow, 1.0f, 0.55f);

    ForceField_AddLayer(&s_fvolFld, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = 1.0f,      // meter-scale: real gravity is 9.81
    });
    // Turbulence is what makes the licks unsteady. Without it the flame is a
    // smooth plume and reads as a gas burner.
    // noiseScale/noiseSpeed are NOT optional. Left at 0 the curl field samples
    // one fixed point for every particle and never advances, which is not
    // turbulence at all — it is a single constant force shoving the whole flame
    // in one arbitrary direction.
    ForceField_AddLayer(&s_fvolFld, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 0.7f,
        .noiseScale = 2.2f,   // ~0.45 m features: lick-sized, not grain-sized
        .noiseSpeed = 1.1f,
    });
    // Moderate drag. Too little and the lick never stops climbing; too much and
    // it stalls immediately and the flame collapses into a ball — which is what
    // 2.6 did, combined with too short a lifetime.
    // Terminal velocity is buoyancy/drag = 1.0/1.8 = 0.55 m/s. THAT is the
    // number that decides whether a 1 m flame reads as fire or as a blowtorch,
    // and it is invisible in the source unless you divide — see
    // core/tests/flame_motion_test.c.
    ForceField_AddLayer(&s_fvolFld, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 1.8f,
    });

    s_fvolInit = true;
}

// A volume of fire at `pos`. `scale` 1.0 ≈ a 1 m flame. `intensity` 0..1 scales
// particle count and core brightness.
//
// Call every frame for a sustained fire (it emits one frame's worth); call once
// for a burst. The caller does NOT manage blend state: the body is tagged
// VFX_BLEND_ALPHA and the core VFX_BLEND_ADDITIVE, and DrawParticles reopens the
// batch when the mode changes while keeping the depth order intact. That is F1b
// in practice — a glowing body is two populations, never one additive draw.
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale,
                            float intensity)
{
    FVol_InitShared();
    SmokePuff_InitShared();   // the flame's smoke reuses F2's sprites

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    (void)mat;   // black-body colour is physical, not per-element — see below

    if (intensity <= 0.0f) return;
    if (intensity > 1.0f) intensity = 1.0f;

    int nCore = (int)(FVOL_MAX_CORE * intensity);
    int nBody = (int)(FVOL_MAX_BODY * intensity);
    if (nCore < 1) nCore = 1;
    if (nBody < 2) nBody = 2;

    // ── BODY: alpha, black-body ramp, cools into smoke ───────────────────────
    for (int i = 0; i < nBody; i++)
    {
        float ang = Random01() * 2.0f * PI;
        float rad = sqrtf(Random01()) * 0.10f * scale * s_fvolWidthMul;
        Vector3 p = {pos.x + cosf(ang) * rad,
                     pos.y + Random01() * 0.10f * scale,
                     pos.z + sinf(ang) * rad};

        // Longer life is what makes the motion read as SLOW: perceived speed is
        // how fast a particle crosses its own size, not its metres per second.
        // Short-lived particles pop in and out and read as frantic even when
        // their velocity is modest.
        float life = Math_Mix(0.75f, 1.40f, Random01());

        // A dying body ember becomes smoke. This is the hand-off that the old
        // implementation was missing entirely, and it is why the flame reads as
        // combustion rather than as a glowing shape: the eye follows fuel
        // through hot -> cooling -> smoke as one continuous process.
        ParticleConfig smoke = {
            .position = p,
            .velocity = {0.0f, 0.22f * scale, 0.0f},
            .radius = Math_Mix(0.10f, 0.26f, Random01()) * scale,
            .lifetime = Math_Mix(0.9f, 1.7f, Random01()),
            .colorStart = (Color){52, 46, 42, (unsigned char)(70.0f * s_fvolSmokeAmt)},
            .colorEnd = (Color){30, 28, 27, 0},
            .forceField = &s_smokePuffFld,
            .radiusCurve = &s_smokePuffGrow,
            .render.texture = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.7f,
        };
        s_fvolSmokeSeed = smoke;

        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = {cosf(ang) * 0.06f * scale,
                         Math_Mix(0.45f, 0.75f, Random01()) * scale * s_fvolRiseMul,
                         sinf(ang) * 0.06f * scale},
            .radius = Math_Mix(0.09f, 0.20f, powf(Random01(), 1.5f)) * scale,
            .lifetime = life,
            .colorStart = WHITE,
            .colorEnd = (Color){44, 40, 38, 0},
            .gradient = &s_fvolBodyGrad,
            .forceField = &s_fvolFld,
            .radiusCurve = &s_fvolGrow,
            .alphaCurve = &s_fvolFade,
            .speedCurve = &s_fvolRise,
            .render.texture = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 1.4f,
            // Only some embers make smoke, otherwise the fire is smothered by it.
            .onDeathEmit = (s_fvolSmokeAmt > 0.0f && (i % 3) == 0) ? &s_fvolSmokeSeed : NULL,
            .onDeathEmitCount = 1,
        });
    }

    // ── CORE: additive, small, short-lived — the actual light source ─────────
    // Kept deliberately small. A large additive core is what turned the old
    // implementation into glowing gas; the core should read as the incandescent
    // heart glimpsed THROUGH the body, not as the flame itself.
    for (int i = 0; i < nCore; i++)
    {
        float ang = Random01() * 2.0f * PI;
        float rad = sqrtf(Random01()) * 0.05f * scale * s_fvolWidthMul;
        unsigned char a = (unsigned char)(255.0f * (s_fvolCoreAlpha < 0.0f ? 0.0f :
                                          s_fvolCoreAlpha > 1.0f ? 1.0f : s_fvolCoreAlpha));
        SpawnParticle((ParticleConfig){
            .position = {pos.x + cosf(ang) * rad,
                         pos.y + Random01() * 0.08f * scale,
                         pos.z + sinf(ang) * rad},
            // Barely rises. The incandescent heart of a fire sits at the BASE
            // and flickers in place; it is the cooled body that travels. Giving
            // the core real velocity turns it into upward-shooting bright dots —
            // small, additive and fast is the exact recipe for "sparks", and it
            // was crossing 4x its own diameter per life.
            .velocity = {0.0f, Math_Mix(0.25f, 0.45f, Random01()) * scale * s_fvolRiseMul, 0.0f},
            .radius = Math_Mix(0.05f, 0.11f, Random01()) * scale,
            .lifetime = Math_Mix(0.35f, 0.60f, Random01()),
            .colorStart = VC_WithAlpha(WHITE, a),
            .colorEnd = (Color){96, 26, 8, 0},
            .gradient = &s_fvolCoreGrad,
            .forceField = &s_fvolFld,
            .alphaCurve = &s_fvolFade,
            .speedCurve = &s_fvolRise,
            .render.blendMode = VFX_BLEND_ADDITIVE,   // it EMITS light
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 2.0f,
        });
    }

    // Fire lights its surroundings — and with E2 it will light the caster too.
    // Flicker so the light is alive rather than a steady lamp.
    float flick = 0.85f + VC_Flicker01((float)GetTime() * 7.0f, pos.x) * 0.3f;
    VFXLight_Spawn(pos, (Color){255, 150, 60, 255},
                   2.2f * scale * flick, 0.12f, VFX_PRIORITY_LOW);
}
