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

static ColorGradient s_fvolCoreGrad = {0}; // additive hot core
static ColorGradient s_fvolBodyGrad = {0}; // black-body body, ends dark
static SkillCurve s_fvolRise = {0};
static SkillCurve s_fvolFade = {0};
static SkillCurve s_fvolGrow = {0};
static ForceField s_fvolFld = {0};
static ParticleConfig s_fvolSmokeSeed; // what a dying body ember becomes
static bool s_fvolInit = false;

static float s_fvolCoreAlpha = 0.55f;
static float s_fvolSmokeAmt = 1.0f;
// Rise speed, as a multiplier on everything vertical. Meter-scale is unforgiving
// here: a 1 m flame whose particles travel 1-2 m in their 0.5 s lifetime reads as
// a blowtorch, not as fire — the embers outrun the flame that made them. Visible
// rise for a flame this size is more like 0.3-0.5 m over a particle's life.
static float s_fvolRiseMul = 1.0f;
// Base width. A flame is TALL and narrow; the ratio of base radius to rise
// height is what decides whether it reads as a flame or as a fireball. A wide
// base with a short rise blurs into a ball no matter how good the colour ramp
// is — and enlarging the particles to fill it makes that worse, not better.
static float s_fvolWidthMul = 1.0f;

// E4 flipbook. Two simulated sheets, and they are not interchangeable:
//
//   0 = the F2 round sprites (A/B reference)
//   1 = fire_puff_8x8_flame  — a radial PUFF, one billow per sprite. The
//       building block: several of them scatter into a bed of fire, which is
//       the reference the owner gave (many separate tongues, dark gaps).
//   2 = fire_atlas_8x8_flame — a whole flame COLUMN in one sprite. Right for a
//       single isolated flame, wrong as a block: a few of them merge into one
//       mass because each already IS the fire.
//
// Neither is "better" — they are different scales of the same effect, which is
// why this is one knob with three values rather than a quality setting.
static float s_fvolAtlas = 1.0f;
static Texture2D s_fvolFlameTex = {0};   // the COLUMN sheet
static Texture2D s_fvolPuffTex = {0};    // the PUFF sheet
static float s_fvolBodyCount = 1.0f;   // x on atlas body sprites (perf lever)
static float s_fvolSpread = 1.0f;      // x on how wide the licks are spread
// Body blend: 1 = ADDITIVE (default with the atlas), 0 = the original ALPHA.
//
// F3 chose ALPHA for the body so the flame could be DARKER than its background —
// the old fire_funnel was one additive draw and read as glowing gas. That was
// the right call for a hand-tuned gradient on a round sprite. With a SIMULATED
// sheet it is wrong: the sprite already carries its own density falloff, so
// alpha stacks it into opaque PATCHES, while real flame is translucent and
// accumulates (owner: "lửa mới có độ trong nhất định... còn cái này màu giống
// theo từng mảng"). The cooling tail that needs to occlude is the SMOKE layer,
// which stays alpha and stays lit.
static float s_fvolBodyBlend = 1.0f;
// Per-sprite contribution when the body is additive. Additive ACCUMULATES, so
// this is the knob that decides whether overlapping tongues keep their orange or
// clip to white — and it interacts with the background: the test arena's sky
// sits around 0.35, so the same value that reads as fire against a night scene
// blows out here. Tune it in the scene the effect ships in.
static float s_fvolBodyAlpha = 0.18f;
static SpriteAnim s_fvolFlameAnim = {0};
static SpriteAnim s_fvolPuffAnim = {0};
// The longest life a BODY particle can be given below. The flipbook rate is
// derived from it, so the two cannot drift apart.
#define FVOL_BODY_LIFE_MAX 1.40f

static void FVol_InitShared(void)
{
    if (s_fvolInit)
        return;

    Tuning_RegisterFloat("flame_core_alpha", &s_fvolCoreAlpha, 0.55f);
    Tuning_RegisterFloat("flame_smoke_amount", &s_fvolSmokeAmt, 1.0f);
    Tuning_RegisterFloat("flame_rise_mul", &s_fvolRiseMul, 1.0f);
    Tuning_RegisterFloat("flame_width_mul", &s_fvolWidthMul, 1.0f);
    Tuning_RegisterFloat("flame_atlas", &s_fvolAtlas, 1.0f); // 0 sprites/1 puff/2 column
    Tuning_RegisterFloat("flame_body_count", &s_fvolBodyCount, 1.0f);
    Tuning_RegisterFloat("flame_spread", &s_fvolSpread, 1.0f);
    Tuning_RegisterFloat("flame_body_blend", &s_fvolBodyBlend, 1.0f);
    Tuning_RegisterFloat("flame_body_alpha", &s_fvolBodyAlpha, 0.18f);

    // The FLAME channel only — the sheet's smoke channel is a separate file.
    // The particle shader multiplies the whole of rgb by the vertex colour, so a
    // two-channel sheet would tint the fire with its own smoke.
    s_fvolFlameTex = ResourceManager_LoadTexture("assets/textures/fire_atlas_8x8_flame.png");
    if (s_fvolFlameTex.id != 0)
    {
        SetTextureFilter(s_fvolFlameTex, TEXTURE_FILTER_BILINEAR);
        // fps derived from the LONGEST body lifetime (1.7 s), never the average:
        // SpriteAnim advances on absolute age, so a faster rate would run a
        // long-lived particle past the last frame, where the sheet is empty —
        // the flame would VANISH while its alpha curve still says visible.
        // (E4 landmine, learned on the smoke puff.)
        SpriteAnim_Init(&s_fvolFlameAnim, 8, 8, 64, 64.0f / 1.7f, ANIM_ONCE);
    }
    else
        TraceLog(LOG_WARNING, "FlameVolume: fire_atlas_8x8_flame.png missing — "
                              "using F2 sprites (run scripts/flipbook/make.py fire)");

    s_fvolPuffTex = ResourceManager_LoadTexture("assets/textures/fire_puff_8x8_flame.png");
    if (s_fvolPuffTex.id != 0)
    {
        SetTextureFilter(s_fvolPuffTex, TEXTURE_FILTER_BILINEAR);
        // Derived from the longest body life, so the sheet plays exactly ONCE
        // over it and can never run past its empty tail (E4's smoke landmine:
        // SpriteAnim advances on absolute age, so a rate faster than the
        // longest-lived particle makes the flame vanish while its alpha curve
        // still says visible). The puff sim ends in dissipation rather than
        // darkness, so overrunning it would blink, not fade.
        SpriteAnim_Init(&s_fvolPuffAnim, 8, 8, 64, 64.0f / FVOL_BODY_LIFE_MAX,
                        ANIM_ONCE);
    }
    else
        TraceLog(LOG_WARNING, "FlameVolume: fire_puff_8x8_flame.png missing — "
                              "falling back to the column sheet (bake it with "
                              "scripts/flipbook/ti_sim.py fire_puff)");

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
    ColorGradient_AddStop(&s_fvolBodyGrad, 0.78f, (Color){92, 46, 34, 90}); // cooling
    ColorGradient_AddStop(&s_fvolBodyGrad, 1.00f, (Color){44, 40, 38, 0});  // meets smoke

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
                                        .strength = 1.0f, // meter-scale: real gravity is 9.81
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
                                        .noiseScale = 2.2f, // ~0.45 m features: lick-sized, not grain-sized
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
    SmokePuff_InitShared(); // the flame's smoke reuses F2's sprites
    // Which sheet, resolved once. A missing file must fall THROUGH to the other
    // sheet rather than to the F2 sprites: an effect that silently changes
    // scale is harder to diagnose than one that silently changes look.
    const bool wantPuff = (s_fvolAtlas > 0.5f) && (s_fvolAtlas < 1.5f);
    const bool usePuff = wantPuff && (s_fvolPuffTex.id != 0);
    const bool useAtlas = (s_fvolAtlas > 0.5f)
                          && (usePuff || s_fvolFlameTex.id != 0);
    const Texture2D bodyTex = usePuff ? s_fvolPuffTex : s_fvolFlameTex;
    SpriteAnim *bodyAnim = usePuff ? &s_fvolPuffAnim : &s_fvolFlameAnim;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    (void)mat; // black-body colour is physical, not per-element — see below

    if (intensity <= 0.0f)
        return;
    if (intensity > 1.0f)
        intensity = 1.0f;

    int nCore = (int)(FVOL_MAX_CORE * intensity);
    int nBody = (int)(FVOL_MAX_BODY * intensity);
    if (useAtlas)
    {
        // FAR fewer, FAR bigger. Each atlas sprite already carries a whole
        // simulated flame — stacking 22 of them is both why the fire read as one
        // solid mass and why the frame rate collapsed to 22 fps on a single
        // flame: every sprite is a large alpha-blended quad, and with the
        // flipbook cross-fade each one draws TWICE. Overdraw, not particle
        // count, is the cost here. (Same lesson as E4's smoke: a flipbook sprite
        // is a simulation, not a puff — do not stack it like one.)
        // The PUFF sheet is one billow, not a whole fire, so the bed needs more
        // of them — but each is also SMALLER (see the radius below), so the
        // overdraw that capped the column sheet at 6 sprites stays paid for.
        nBody = (int)((usePuff ? 10.0f : 6.0f) * intensity * s_fvolBodyCount);
        nCore = (int)(3.0f * intensity);
    }
    if (nCore < 1)
        nCore = 1;
    if (nBody < 2)
        nBody = 2;

    // ── BODY: alpha, black-body ramp, cools into smoke ───────────────────────
    for (int i = 0; i < nBody; i++)
    {
        float ang = Random01() * 2.0f * PI;
        // Spread the licks across a BASE rather than stacking them on one
        // axis. The reference the owner gave is a row of separate tongues over a
        // wide bed of fire, not a single column — with the atlas each sprite is
        // already a tongue, so the composition's job is where to place them.
        float rad = sqrtf(Random01()) * (useAtlas ? 0.34f * s_fvolSpread : 0.10f)
                    * scale * s_fvolWidthMul;
        Vector3 p = {pos.x + cosf(ang) * rad,
                     pos.y + Random01() * 0.10f * scale,
                     pos.z + sinf(ang) * rad};

        // Longer life is what makes the motion read as SLOW: perceived speed is
        // how fast a particle crosses its own size, not its metres per second.
        // Short-lived particles pop in and out and read as frantic even when
        // their velocity is modest.
        float life = Math_Mix(0.75f, FVOL_BODY_LIFE_MAX, Random01());

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
            // Bigger with the atlas: the sprite IS the flame, so it has to be
            // read at flame size rather than as one blob among many.
            .radius = (usePuff  ? Math_Mix(0.20f, 0.32f, Random01())
                       : useAtlas ? Math_Mix(0.30f, 0.46f, Random01())
                                  : Math_Mix(0.09f, 0.20f, powf(Random01(), 1.5f))) * scale,
            .lifetime = life,
            .colorStart = (useAtlas && s_fvolBodyBlend > 0.5f)
                              ? VC_WithAlpha(WHITE, (unsigned char)(255.0f * s_fvolBodyAlpha))
                              : WHITE,
            .colorEnd = (Color){44, 40, 38, 0},
            .gradient = &s_fvolBodyGrad,
            .forceField = &s_fvolFld,
            .radiusCurve = &s_fvolGrow,
            .alphaCurve = &s_fvolFade,
            .speedCurve = &s_fvolRise,
            .render.blendMode = (useAtlas && s_fvolBodyBlend > 0.5f)
                                    ? VFX_BLEND_ADDITIVE : VFX_BLEND_ALPHA,
            // Additive accumulates, so each sprite must contribute LESS or a few
            // overlapping tongues clip straight to white.
            .render.emissiveBoost = (useAtlas && s_fvolBodyBlend > 0.5f) ? 1.05f : 1.0f,
            .render.texture = useAtlas ? bodyTex
                                       : s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .spriteAnim = useAtlas ? bodyAnim : NULL,
            // FIRE EMITS LIGHT — it must not be multiplied by the scene's.
            // Lighting is a multiply, so a flame lit by a dim sky turns brown;
            // that is what "the fire went black" was. Only its smoke is lit.
            .render.unlit = 1,
            // Rotation is a property of the SHEET, not of the atlas path. The
            // COLUMN sheet has an UP — spinning it renders the fire upside down
            // (the bug that hid the engine's flipped-quad landmine for so long).
            // The PUFF sheet was simulated with buoyancy and gravity at zero, so
            // it is radially symmetric by construction and has no up to lose:
            // spinning it is legal again, and it is what keeps ten sprites off
            // one sheet from reading as ten copies of the same billow.
            .rotation = (!useAtlas || usePuff) ? Random01() * 2.0f * PI : 0.0f,
            .angularVelocity = (!useAtlas || usePuff)
                                   ? (Random01() - 0.5f) * (usePuff ? 0.5f : 1.4f)
                                   : 0.0f,
            // Only some embers make smoke, otherwise the fire is smothered by it.
            // .onDeathEmit = (s_fvolSmokeAmt > 0.0f && (i % 3) == 0) ? &s_fvolSmokeSeed : NULL,
            // .onDeathEmitCount = 1,
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
        unsigned char a = (unsigned char)(255.0f * (s_fvolCoreAlpha < 0.0f ? 0.0f : s_fvolCoreAlpha > 1.0f ? 1.0f
                                                                                                           : s_fvolCoreAlpha));
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
            .render.blendMode = VFX_BLEND_ADDITIVE, // it EMITS light
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 2.0f,
        });
    }

    // Fire lights its surroundings — and with E2 it will light the caster too.
    // Flicker so the light is alive rather than a steady lamp.
    // ONE light, refreshed on an interval — not one per frame. This is called
    // every frame for a sustained fire, so a 0.12 s light spawned each time left
    // ~7 duplicates alive at once, all at the same spot: they filled the 16-slot
    // pool, starved every other effect, and still lit the scene like a single
    // lamp. The refresh interval only has to beat the lifetime.
    static float s_lightTimer = 0.0f;
    s_lightTimer -= GetFrameTime();
    if (s_lightTimer <= 0.0f)
    {
        s_lightTimer = 0.10f;
        float flick = 0.85f + VC_Flicker01((float)GetTime() * 7.0f, pos.x) * 0.3f;
        // Lifted to mid-flame height. At the base the light sits IN the ground
        // plane, so the vector to it is nearly parallel to the floor and
        // dot(N, toL) collapses to ~0.1 — the ground receives almost nothing no
        // matter how bright the light is. Geometry first, intensity second.
        Vector3 lightPos = {pos.x, pos.y + 0.55f * scale, pos.z};
        VFXLight_Spawn(lightPos, (Color){255, 150, 60, 255},
                       4.0f * scale * flick, 0.13f, VFX_PRIORITY_LOW);
    }
}
