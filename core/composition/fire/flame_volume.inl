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
// Volume path only: the particle's HEAT over its life, multiplying the sheet's
// emission before the ramp lookup. Hot at birth, cooling to nothing — this is
// the axis the black-body ramp is travelled along.
static SkillCurve s_fvolCool = {0};
static ForceField s_fvolFld = {0};
static ParticleConfig s_fvolSmokeSeed; // what a dying body ember becomes
static bool s_fvolInit = false;

static float s_fvolCoreAlpha = 0.55f;
static float s_fvolSmokeAmt = 1.0f;
// Rise speed, as a multiplier on everything vertical. Meter-scale is unforgiving
// here: a 1 m flame whose particles travel 1-2 m in their 0.5 s lifetime reads as
// a blowtorch, not as fire — the embers outrun the flame that made them. Visible
// rise for a flame this size is more like 0.3-0.5 m over a particle's life.
static float s_fvolRiseMul = 1.35f;
// Base width. A flame is TALL and narrow; the ratio of base radius to rise
// height is what decides whether it reads as a flame or as a fireball.
static float s_fvolWidthMul = 0.55f;

// E4 flipbook. Two simulated sheets, and they are not interchangeable:
//
//   0 = the F2 round sprites (A/B reference)
//   1 = fire_puff_8x8_flame  — a radial PUFF, one billow per sprite. The
//       building block: several of them scatter into a bed of fire, which is
//       the reference the owner gave (many separate tongues, dark gaps).
//   2 = fire_tongue_8x8_flame — a whole flame COLUMN in one sprite. Right for a
//       single isolated flame, wrong as a block: a few of them merge into one
//       mass because each already IS the fire.
//
// Neither is "better" — they are different scales of the same effect, which is
// why this is one knob with three values rather than a quality setting.
static float s_fvolAtlas = 1.0f;
static Texture2D s_fvolFlameTex = {0};   // the COLUMN sheet
static Texture2D s_fvolPuffTex = {0};    // the PUFF sheet
static float s_fvolBodyCount = 1.0f;   // x on atlas body sprites (perf lever)
// How many body sprites are ALIVE at once — the quantity the eye judges, and
// the one the emission rate is derived from (rate = live / average lifetime).
// TRANSLUCENCY AND COUNT ARE ONE DECISION, NOT TWO.
//
// The sheet was 41.8% opaque texels (A > 0.9) against the smoke puff's 6.0%,
// and an opaque sprite HIDES the ones behind it — ninety of them stack as
// ninety cards, which is the "mảng mảng" the owner kept reporting. Re-baking at
// --density-scale 0.7 --flame-extinction 0.3 brings that to 15.6%, so sprites
// accumulate instead of occluding.
//
// But a translucent sprite also carries less mass, and at the old 90 the flame
// came out thin and small. The two numbers have to move together: lowering one
// without raising the other is how this looked WORSE at each half-step. Note
// the owner already measured the other direction — cutting the count to 26 made
// the patchiness more visible, not less, because it exposes each silhouette.
static float s_fvolBodyLive = 55.0f;
// Multiplier on the puff body's radius. Count and size buy the same cohesion at
// the same fill cost; size is the cheaper one in draw calls. Which is right is
// a look judgement, so both are tunables.
static float s_fvolBodySize = 1.0f;
static float s_fvolSpread = 1.0f;      // x on how wide the licks are spread
// Body blend: 0 = ALPHA (default), 1 = ADDITIVE.
//
// F3 chose ALPHA for the body so the flame could be DARKER than its background —
// the old fire_funnel was one additive draw and read as glowing gas. That was
// the right call for a hand-tuned gradient on a round sprite. With a SIMULATED
// sheet it is wrong: the sprite already carries its own density falloff, so
// alpha stacks it into opaque PATCHES, while real flame is translucent and
// accumulates (owner: "lửa mới có độ trong nhất định... còn cái này màu giống
// theo từng mảng"). The cooling tail that needs to occlude is the SMOKE layer,
// which stays alpha and stays lit.
static float s_fvolBodyBlend = 0.0f;
// Per-sprite contribution when the body is additive. Additive ACCUMULATES, so
// this is the knob that decides whether overlapping tongues keep their orange or
// clip to white — and it interacts with the background: the test arena's sky
// sits around 0.35, so the same value that reads as fire against a night scene
// blows out here. Tune it in the scene the effect ships in.
// Volume fire is deliberately many low-coverage layers.
//
// 0.35 SINCE 19/08/2026, up from 0.12. The old note here said 0.18 fused the hot
// cores into "opaque white marbles at the emitter foot", and that observation is
// left standing because it was real — but it predates the Dot H packed-sheet
// build, and it was made while tuning.cfg held a bloom threshold below 1.0 that
// veiled the whole frame (see the harness pin). Re-measured on the pinned
// configuration, every metric improves on BOTH backgrounds and no marbles
// appear:
//        white: structure 0.095 -> 0.179, detail 0.013 -> 0.028, |d| 0.205 -> 0.341
//        dark : structure 0.512 -> 0.536, detail 0.054 -> 0.061, |d| 0.719 -> 0.759
//
// This is the negative-contrast law (§7.6c) doing what tripling the emissive
// could not: on a bright background an effect cannot out-shine its surroundings,
// so legibility comes from the core OCCLUDING them. What this flame was missing
// was opacity, not light.
static float s_fvolBodyAlpha = 0.35f;
static SpriteAnim s_fvolFlameAnim = {0};
static SpriteAnim s_fvolPuffAnim = {0};

// ── Đợt H — THE VOLUME PATH ─────────────────────────────────────────────────
//
// 1 (default) = ONE population off the packed 4-channel sheet, drawn
// premultiplied. 0 = the F3 three-population build (additive core + alpha body
// + smoke), kept for A/B because "which reads better" is a look judgement.
//
// It replaces the older build for two reasons that turn out to be one:
//
//   LOOK. The split sheet's RGB is a flat 255/255/255 mask, so the whole quad
//   takes ONE vertex colour and a sprite can be bright in the middle but never
//   WHITE-hot with an orange rim. Every zone of colour the eye looks for in
//   fire had to be faked by placing differently-tinted sprites next to each
//   other, which is why it read as tinted fog. The packed sheet's R is a real
//   per-texel temperature field (mean 34, sd 44), so the zoning is per pixel.
//
//   COST. Three populations DEPTH-SORT INTO EACH OTHER, and the batch key is
//   texture+blend+unlit+grid+boost — so every alternation between core, body
//   and smoke is an rlEnd plus two rlDrawRenderBatchActive flushes. That is
//   why "a few dozen particles" already cost frames while cutting 700 sprites
//   to 18 barely moved them (particle_system.c's perf instrument records that
//   measurement). One population, one texture, one blend mode collapses the
//   batch count to ~1, and premultiplied is what makes one population able to
//   emit and occlude at the same time.
static float s_fvolVolume = 1.0f;
static Texture2D s_fvolVolumeTex = {0};
static SpriteAnim s_fvolVolumeAnim = {0};
// Generated occupied bounds for the directionless puff. They reduce the cost
// of its transparent margins while SpriteAnim keeps the original cell pivot,
// so this is not an authored direction or a silhouette change.
#include "flame_volume_puff_metadata.inl"
// Exposure on the sheet's emission before it indexes the ramp — "how much of
// the flame is white-hot". The sim normalises emission to its own 99.5th
// percentile and has no idea how bright this effect should read, so this is
// the knob that decides incandescent vs smouldering.
static float s_fvolHeatGain = 1.05f;
// Radiance gain on the flame half. SEPARATE from heatGain on purpose: heatGain
// moves the sprite along the ramp (what COLOUR it is), this moves how much light
// it throws (how BRIGHT it is). Conflating them means you cannot have a deep-red
// flame that is genuinely bright, or a pale one that is dim.
//
// It has to be well above 1: the sheet's emission averages 0.13 of full scale
// (it is normalised to its own 99.5th percentile), so at unity gain the whole
// flame lands near black once ACES has had it. Above ~4 the hottest texels cross
// 1.0 into the HDR buffer's headroom, which is what finally makes the core blow
// out and bloom instead of clipping to a flat orange.
static float s_fvolEmissive = 4.8f;
// ── SMOKINESS IS A COMPOSITION DECISION, NOT AN ASSET ONE ───────────────────
//
// The sheet is directionless by construction (the puff sim runs at zero gravity
// and zero buoyancy so sprites can be spun and scattered without reading as
// copies), and these two keep its SMOKINESS directionless in the same sense:
// nothing about "petrol fire" vs "burning leaves" vs "a clean flame" is baked
// into the texture. The R:G ratio was the last thing that was, and a second
// bake for it would have been the wrong unit — one greyscale puff has to serve
// all three.
//
//   petrol      smokeGain 1.5  tint  20, 18, 17   (heavy, black)
//   leaves      smokeGain 1.2  tint 214, 210, 198 (light, white)
//   clean flame smokeGain 0.15 tint  90, 82, 76   (almost none)
//
// Measured on the shipping sheet: emission averages 37.5 against soot 155.1, a
// ratio of 0.24 — heavily smoke-dominated, which is why the default reads as a
// large sooty fire rather than a torch.
static float s_fvolSmokeGain = 0.95f;
static float s_fvolSmokeR = 82.0f, s_fvolSmokeG = 74.0f, s_fvolSmokeB = 69.0f;
// Ramp LUTs, one per material, baked lazily. THIS is where fire's colour lives
// now — the sheet is greyscale on purpose, so pointing this at another gradient
// turns the same simulation into purple or blue magic fire with no re-bake.
static Texture2D s_fvolRampLUT[VC_MAT_COUNT];
static ColorGradient s_fvolHeatGrad[VC_MAT_COUNT];
// The longest life a BODY particle can be given below. The flipbook rate is
// derived from it, so the two cannot drift apart.
#define FVOL_BODY_LIFE_MAX 1.40f
// Averages of the lifetime ranges the spawns below actually use. The emission
// rate is derived from these, so if a lifetime changes, change these with it —
// they are two halves of one number (live count = rate x lifetime).
// Spread of the per-particle flipbook phase. Bought out of the playback rate,
// not added on top of it — see where the rate is derived.
#define FVOL_BODY_PHASE_MAX 0.50f
#define FVOL_BODY_LIFE_AVG 1.075f   // Mix(0.75, 1.40)
#define FVOL_CORE_LIFE_AVG 0.30f
#define FVOL_MAX_EMITTERS 12

typedef struct {
    bool active;
    bool stopping;
    Vector3 pos;
    Vector3 wind;
    VC_MaterialId matId;
    float scale;
    float intensity;
    float bodyAccum;
    float coreAccum;
    float lightTimer;
    float legacyFeedAge;
    float seed;
    unsigned int generation;
} VC_FlameEmitter;

static VC_FlameEmitter s_fvolEmitters[FVOL_MAX_EMITTERS];
static int s_fvolNextEmitter = 0;
static unsigned int s_fvolNextGeneration = 1;

static void FVol_InitShared(void)
{
    if (s_fvolInit)
        return;

    Tuning_RegisterFloat("flame_core_alpha", &s_fvolCoreAlpha, 0.55f);
    Tuning_RegisterFloat("flame_smoke_amount", &s_fvolSmokeAmt, 1.0f);
    Tuning_RegisterFloat("flame_rise_mul", &s_fvolRiseMul, 1.35f);
    Tuning_RegisterFloat("flame_width_mul", &s_fvolWidthMul, 0.55f);
    Tuning_RegisterFloat("flame_atlas", &s_fvolAtlas, 1.0f); // 0 sprites/1 puff/2 column
    Tuning_RegisterFloat("flame_body_count", &s_fvolBodyCount, 1.0f);
    Tuning_RegisterFloat("flame_body_live", &s_fvolBodyLive, 55.0f);
    Tuning_RegisterFloat("flame_body_size", &s_fvolBodySize, 1.0f);
    Tuning_RegisterFloat("flame_spread", &s_fvolSpread, 1.0f);
    Tuning_RegisterFloat("flame_body_blend", &s_fvolBodyBlend, 0.0f);
    /* The default here WINS over the static initialiser above — Tuning_RegisterFloat
       assigns it. Changing one without the other is a silent no-op, and was. */
    Tuning_RegisterFloat("flame_body_alpha", &s_fvolBodyAlpha, 0.35f);
    Tuning_RegisterFloat("flame_volume", &s_fvolVolume, 1.0f);
    Tuning_RegisterFloat("flame_heat_gain", &s_fvolHeatGain, 1.05f);
    Tuning_RegisterFloat("flame_emissive", &s_fvolEmissive, 4.8f);
    Tuning_RegisterFloat("flame_smoke_gain", &s_fvolSmokeGain, 0.95f);
    Tuning_RegisterFloat("flame_smoke_r", &s_fvolSmokeR, 82.0f);
    Tuning_RegisterFloat("flame_smoke_g", &s_fvolSmokeG, 74.0f);
    Tuning_RegisterFloat("flame_smoke_b", &s_fvolSmokeB, 69.0f);

    // The packed VOLUME sheet — the same sim as the split above, delivered with
    // its temperature field intact. Missing file falls through to the legacy
    // path rather than drawing nothing.
    const VFX_SurfaceProfile *volProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_VOLUME);
    s_fvolVolumeTex = volProfile != NULL ? volProfile->body : (Texture2D){0};
    if (s_fvolVolumeTex.id != 0)
    {
        SetTextureFilter(s_fvolVolumeTex, TEXTURE_FILTER_BILINEAR);
        // Same derivation as the puff sheet: rate from the LONGEST life plus
        // the largest phase, so no sprite ever runs past frame 64 into the
        // empty tail (the E4 landmine — the flame would vanish while its alpha
        // curve still said visible).
        SpriteAnim_Init(&s_fvolVolumeAnim, 8, 8, 64,
                        64.0f / (FVOL_BODY_LIFE_MAX + FVOL_BODY_PHASE_MAX),
                        ANIM_ONCE);
        SpriteAnim_SetFrameMetadata(&s_fvolVolumeAnim, s_fvolVolumeFrameMeta,
                                    (int)(sizeof(s_fvolVolumeFrameMeta) /
                                          sizeof(s_fvolVolumeFrameMeta[0])));
    }
    else
        TraceLog(LOG_WARNING, "FlameVolume: fire_puff_8x8_volume.png missing — "
                              "volume path disabled, using the split sheets. Bake "
                              "it per assets/INDEX.md (pack.py WITHOUT --split)");

    // The FLAME channel only — the sheet's smoke channel is a separate file.
    // The particle shader multiplies the whole of rgb by the vertex colour, so a
    // two-channel sheet would tint the fire with its own smoke.
    const VFX_SurfaceProfile *tongueProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_TONGUE);
    s_fvolFlameTex = tongueProfile != NULL ? tongueProfile->body : (Texture2D){0};
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
        TraceLog(LOG_WARNING, "FlameVolume: fire_tongue_8x8_flame.png missing — "
                              "using F2 sprites (run scripts/flipbook/make.py fire_tongue)");

    const VFX_SurfaceProfile *puffProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_PUFF);
    s_fvolPuffTex = puffProfile != NULL ? puffProfile->body : (Texture2D){0};
    if (s_fvolPuffTex.id != 0)
    {
        SetTextureFilter(s_fvolPuffTex, TEXTURE_FILTER_BILINEAR);
        // Derived from the longest body life, so the sheet plays exactly ONCE
        // over it and can never run past its empty tail (E4's smoke landmine:
        // SpriteAnim advances on absolute age, so a rate faster than the
        // longest-lived particle makes the flame vanish while its alpha curve
        // still says visible). The puff sim ends in dissipation rather than
        // darkness, so overrunning it would blink, not fade.
        // Rate derived from the longest life PLUS the largest phase offset, so
        // the two together still land inside the 64 frames. At the old
        // 64/1.40 = 45.7 fps a full-life sprite consumed the whole sheet
        // exactly, which left no room for a phase at all — and without a phase
        // every sprite spawned in the same frame holds the same frame for its
        // whole life (SpriteAnim advances on ABSOLUTE age). 64/1.90 = 33.7 fps
        // leaves 0.5 s of phase: (1.40 + 0.50) x 33.0 = 62.7 frames.
        SpriteAnim_Init(&s_fvolPuffAnim, 8, 8, 64,
                        64.0f / (FVOL_BODY_LIFE_MAX + FVOL_BODY_PHASE_MAX),
                        ANIM_ONCE);
    }
    else
        TraceLog(LOG_WARNING, "FlameVolume: fire_puff_8x8_flame.png missing — "
                              "falling back to the tongue sheet (bake it with "
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

    // Cooling. Stays near full heat for the first third — combustion does not
    // start fading the instant fuel ignites — then falls away, which is what
    // walks the sprite down the black-body ramp into soot.
    FloatCurve_AddStop(&s_fvolCool, 0.0f, 1.0f);
    FloatCurve_AddStop(&s_fvolCool, 0.30f, 0.92f);
    FloatCurve_AddStop(&s_fvolCool, 0.65f, 0.45f);
    FloatCurve_AddStop(&s_fvolCool, 1.0f, 0.05f);

    // Narrows as it rises — a flame tapers, unlike smoke which only expands.
    FloatCurve_AddStop(&s_fvolGrow, 0.0f, 0.70f);
    FloatCurve_AddStop(&s_fvolGrow, 0.25f, 1.00f);
    FloatCurve_AddStop(&s_fvolGrow, 0.60f, 0.75f);
    FloatCurve_AddStop(&s_fvolGrow, 1.0f, 0.35f);

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

// ── The heat ramp: temperature -> colour ────────────────────────────────────
//
// Indexed by the SHEET's emission channel, not by age: t=0 is the coolest texel
// of a sprite and t=1 the hottest, so one billow spans the whole ramp at once.
// That is the difference from s_fvolBodyGrad above, which is indexed by a
// particle's age and therefore paints a whole sprite one colour.
//
// Keeping this in code rather than in the texture is what makes magic fire
// cheap: VC_MAT_FIRE gets an authored black-body curve, and every other element
// gets the same CURVE SHAPE re-coloured from its material — white-hot core,
// element hue through the middle, near-black at the cold end. A purple flame is
// still a flame because the shape of the falloff is what reads as combustion;
// the hue is just paint.
static const ColorGradient *FVol_HeatGradient(VC_MaterialId matId)
{
    if (matId < 0 || matId >= VC_MAT_COUNT)
        matId = VC_MAT_FIRE;
    ColorGradient *g = &s_fvolHeatGrad[matId];
    if (g->count > 0)
        return g;

    if (matId == VC_MAT_FIRE)
    {
        // Authored black-body. Weighted so most of the range is orange and only
        // the top lands on white — an even spread reads as a gradient swatch
        // rather than as burning.
        ColorGradient_AddStop(g, 0.00f, (Color){18, 6, 3, 255});      // cold soot
        ColorGradient_AddStop(g, 0.18f, (Color){120, 26, 8, 255});    // dull red
        ColorGradient_AddStop(g, 0.40f, (Color){214, 74, 18, 255});   // red-orange
        ColorGradient_AddStop(g, 0.62f, (Color){255, 140, 34, 255});  // orange
        ColorGradient_AddStop(g, 0.82f, (Color){255, 206, 104, 255}); // amber
        ColorGradient_AddStop(g, 1.00f, (Color){255, 250, 232, 255}); // white-hot
        return g;
    }

    // Derived: the same falloff shape carrying the element's own identity.
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color body = mat->body;
    Color glow = mat->glow;
    ColorGradient_AddStop(g, 0.00f, (Color){(unsigned char)(body.r / 8),
                                            (unsigned char)(body.g / 8),
                                            (unsigned char)(body.b / 8), 255});
    ColorGradient_AddStop(g, 0.18f, (Color){(unsigned char)(body.r / 3),
                                            (unsigned char)(body.g / 3),
                                            (unsigned char)(body.b / 3), 255});
    ColorGradient_AddStop(g, 0.45f, VC_WithAlpha(body, 255));
    ColorGradient_AddStop(g, 0.72f, VC_WithAlpha(glow, 255));
    ColorGradient_AddStop(g, 1.00f, VC_WithAlpha(ColorLerp(glow, WHITE, 0.80f), 255));
    return g;
}

// Clamped here rather than at each tunable: tuning.cfg hot-reloads raw floats
// straight into these, so a typo would wrap the byte cast instead of saturating.
static Color FVol_SmokeTint(void)
{
    float r = s_fvolSmokeR, g = s_fvolSmokeG, b = s_fvolSmokeB;
    if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
    if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
    if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

static Texture2D FVol_RampLUT(VC_MaterialId matId)
{
    if (matId < 0 || matId >= VC_MAT_COUNT)
        matId = VC_MAT_FIRE;
    if (s_fvolRampLUT[matId].id == 0)
        s_fvolRampLUT[matId] = ColorGradient_BakeLUT(FVol_HeatGradient(matId), 64);
    return s_fvolRampLUT[matId];
}

// A volume of fire at `pos`. `scale` 1.0 ≈ a 1 m flame. `intensity` 0..1 scales
// particle count and core brightness.
//
// Call every frame for a sustained fire (it emits one frame's worth); call once
// for a burst. The caller does NOT manage blend state: the body is tagged
// VFX_BLEND_ALPHA and the core VFX_BLEND_ADDITIVE, and DrawParticles reopens the
// batch when the mode changes while keeping the depth order intact. That is F1b
// in practice — a glowing body is two populations, never one additive draw.
static void FVol_Emit(VC_FlameEmitter *emitter, float dt)
{
    Vector3 pos = emitter->pos;
    VC_MaterialId matId = emitter->matId;
    float scale = emitter->scale;
    float intensity = emitter->intensity;
    FVol_InitShared();
    SmokePuff_InitShared(); // the flame's smoke reuses F2's sprites
    // Which sheet, resolved once. A missing file must fall THROUGH to the other
    // sheet rather than to the F2 sprites: an effect that silently changes
    // scale is harder to diagnose than one that silently changes look.
    // The volume path supersedes the atlas choice entirely: it is a different
    // sheet with a different decoder, not another value of `flame_atlas`.
    const bool useVolume = (s_fvolVolume > 0.5f) && (s_fvolVolumeTex.id != 0);
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

    // ── EMISSION IS A RATE, NOT A COUNT PER CALL ─────────────────────────────
    //
    // This function is called EVERY FRAME for a sustained fire, and it used to
    // spawn a fixed number of sprites each time. Two consequences, one of them
    // a bug and one of them the reason a single flame cost the frame rate:
    //
    //   1. Frame-rate dependent. At 60 fps it emitted 13 sprites/frame = 780 per
    //      SECOND; at 20 fps, 260. The fire literally changed density with the
    //      frame rate, and it "stabilised" at ~20 fps because emitting less is
    //      what let the frame rate recover — a feedback loop, not a budget.
    //   2. The live count was rate x lifetime: 780/s against a 0.75-1.40 s body
    //      life is ~860 live sprites for ONE flame, each a large blended quad
    //      drawn TWICE by the atlas cross-fade. ~1700 quads/frame.
    //
    // The knob is now the LIVE COUNT, which is the thing an artist can actually
    // see, and the rate is derived from it: rate = live / averageLifetime. The
    // accumulator carries the fraction, so 0.25 sprites per frame at 60 fps
    // emits one every fourth frame instead of rounding to zero or to one.
    const float dtNow = dt;
    float *bodyAccum = &emitter->bodyAccum;
    float *coreAccum = &emitter->coreAccum;

    // ── VOLUME PATH — one population, one draw ───────────────────────────────
    //
    // No core and no separate smoke: the sheet carries emission, soot and
    // self-shadow in the same texels, so what used to be three interleaved
    // populations is one. That is the whole fps story — see the note on
    // s_fvolVolume.
    if (useVolume)
    {
        // Announce on CHANGE, never once at startup: `flame_volume` hot-reloads,
        // so a one-shot line scrolls away long before the value that matters
        // arrives — and then "my edit did nothing" is indistinguishable from
        // "my edit never applied" (core/CLAUDE.md §4).
        {
            static float lastHeat = -1.0f, lastLive = -1.0f;
            static int lastMat = -1;
            if (lastHeat != s_fvolHeatGain || lastLive != s_fvolBodyLive ||
                lastMat != (int)matId)
            {
                lastHeat = s_fvolHeatGain; lastLive = s_fvolBodyLive;
                lastMat = (int)matId;
                TraceLog(LOG_INFO,
                         "FLAME volume path ACTIVE: sheet=%u ramp=%u heatGain=%.2f "
                         "emissive=%.2f live=%.0f mat=%d blend=PREMULTIPLIED",
                         (unsigned)s_fvolVolumeTex.id,
                         (unsigned)FVol_RampLUT(matId).id, s_fvolHeatGain,
                         s_fvolEmissive, s_fvolBodyLive, (int)matId);
            }
        }
        const float live = s_fvolBodyLive * intensity * s_fvolBodyCount;
        *bodyAccum += dtNow * (live / FVOL_BODY_LIFE_AVG);
        int n = (int)*bodyAccum;
        *bodyAccum -= (float)n;
        if (n > 24) { n = 24; *bodyAccum = 0.0f; }
        if (n < 0) n = 0;

        const Texture2D ramp = FVol_RampLUT(matId);
        for (int i = 0; i < n; i++)
        {
            float ang = Random01() * 2.0f * PI;
            // The volume sheet is one billow, therefore its emitter is the
            // silhouette author. A compact foot plus a longer vertical travel
            // reads as flame; the old 0.34 m disk made a wide fireball even
            // when the asset itself was completely directionless.
            // Compact foot plus a longer vertical travel gives a tapered flame cone.
            float rad = sqrtf(Random01()) * 0.16f * s_fvolSpread * scale * s_fvolWidthMul;
            Vector3 p = {pos.x + cosf(ang) * rad,
                         pos.y + Random01() * 0.05f * scale,
                         pos.z + sinf(ang) * rad};
            float life = Math_Mix(0.80f, FVOL_BODY_LIFE_MAX, Random01());

            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = {cosf(ang) * 0.025f * scale,
                             Math_Mix(0.65f, 0.95f, Random01()) * scale * s_fvolRiseMul,
                             sinf(ang) * 0.025f * scale},
                .radius = Math_Mix(0.18f, 0.44f, powf(Random01(), 1.4f))
                          * s_fvolBodySize * scale,
                .lifetime = life,
                // In volume mode colorStart.a is a per-billboard coverage
                // multiplier; the sheet's A is the local gas coverage.  It
                // still must use the authored body-alpha dial: a hard-coded
                // 200/255 made broad soft puffs stack into one compressed
                // opaque mass before their internal density could read.
                .colorStart = VC_WithAlpha(WHITE,
                                           (unsigned char)(255.0f * s_fvolBodyAlpha)),
                .alphaCurve = &s_fvolFade,
                .speedCurve = &s_fvolRise,
                // Cooling. In volume mode emissiveCurve is read as the particle's
                // HEAT over life and multiplies the sheet's emission before the
                // ramp lookup — so an ember genuinely slides white -> orange ->
                // soot instead of cross-fading between two tinted sprites.
                .emissiveCurve = &s_fvolCool,
                .radiusCurve = &s_fvolGrow,
                .forceField = &s_fvolFld,
                .render.volumeSheet = 1,
                .render.rampLUT = ramp,
                .render.heatGain = s_fvolHeatGain,
                .render.emissiveBoost = s_fvolEmissive,
                .render.smokeGain = s_fvolSmokeGain,
                .render.smokeTint = FVol_SmokeTint(),
                .render.sixWayLighting = 1,
                .render.sixWayScattering = 1.35f,
                .render.sixWayAbsorption = 1.20f,
                .render.blendMode = VFX_BLEND_PREMULTIPLIED,
                .render.texture = s_fvolVolumeTex,
                // NOT unlit. The volume branch lights only the SOOT half and
                // leaves emission alone, which is what the unlit flag existed to
                // protect — the flag would now switch off the smoke's shading
                // as well, and its whole job is to stop the tail reading flat.
                .spriteAnim = &s_fvolVolumeAnim,
                .spriteAnimPhase = Random01() * FVOL_BODY_PHASE_MAX,
                // Never faster than the derived sheet rate: phase + lifetime
                // must stay inside its 64 authored frames. Slower variation,
                // full phase and mirrors are enough to break clone-lockstep on
                // a directionless asset without baking directional tongues.
                .spriteAnimRate = Math_Mix(0.82f, 1.0f, Random01()),
                .spriteFlipX = Random01() < 0.5f,
                .spriteFlipY = false,
                // A moving source carries its youngest billows along, then
                // releases them into the force field. Generation makes this
                // safe when the 12-slot emitter pool is recycled.
                .followTarget = &emitter->pos,
                .followTargetGeneration = &emitter->generation,
                .followGeneration = emitter->generation,
                .followStrength = 0.70f,
                .rotation = (Random01() - 0.5f) * 0.35f,
                .angularVelocity = (Random01() - 0.5f) * 0.15f,
            });
        }

        emitter->lightTimer -= dtNow;
        if (emitter->lightTimer <= 0.0f)
        {
            emitter->lightTimer = 0.10f;
            float flick = 0.85f + VC_Flicker01(TimeFX_Elapsed() * 7.0f, emitter->seed) * 0.3f;
            Vector3 lightPos = {pos.x, pos.y + 0.55f * scale, pos.z};
            VFXLight_Spawn(lightPos, (Color){255, 150, 60, 255},
                           4.0f * scale * flick, 0.13f, VFX_PRIORITY_LOW);
        }
        return;
    }

    int nCore, nBody;
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
        // Target LIVE sprites. The first pass at this set 14 and the fire fell
        // apart into scattered dots — a correct unit with a wrong value is
        // still wrong, and cutting 700 to 14 was never justified by anything
        // measured. It is a TUNABLE now (`flame_body_live`) precisely because
        // the right value is a judgement about the look, not arithmetic.
        const float bodyLive = s_fvolBodyLive * intensity * s_fvolBodyCount;
        const float coreLive = s_fvolBodyLive * 0.22f * intensity;
        *bodyAccum += dtNow * (bodyLive / FVOL_BODY_LIFE_AVG);
        *coreAccum += dtNow * (coreLive / FVOL_CORE_LIFE_AVG);
    }
    else
    {
        // The pre-atlas path keeps its own densities, expressed the same way.
        *bodyAccum += dtNow * (FVOL_MAX_BODY * intensity / FVOL_BODY_LIFE_AVG);
        *coreAccum += dtNow * (FVOL_MAX_CORE * intensity / FVOL_CORE_LIFE_AVG);
    }
    nBody = (int)*bodyAccum;
    nCore = (int)*coreAccum;
    *bodyAccum -= (float)nBody;
    *coreAccum -= (float)nCore;
    // A long hitch must not dump a hundred sprites in one frame — that is the
    // spike the budget exists to prevent.
    if (nBody > 24) { nBody = 24; *bodyAccum = 0.0f; }
    if (nCore > 8)  { nCore = 8;  *coreAccum = 0.0f; }
    if (nBody < 0) nBody = 0;
    if (nCore < 0) nCore = 0;

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
            .render.sixWayLighting = 1,
            .render.sixWayScattering = 1.25f,
            .render.sixWayAbsorption = 1.10f,
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
            // SIZE, and why it went up when the COUNT came down. At ~215 live
            // sprites the old 0.20-0.32 m billows overlapped by sheer number;
            // at 90 they read as separate dots. Fill cost is count x radius^2,
            // so buying cohesion back with size costs the same as buying it
            // with count — and size also removes the visible EDGES that make a
            // sparse cloud read as debris. Same law SmokePuff uses, and for the
            // same reason: mostly medium with a tail of large ones (the pow
            // weighting), so overlaps look like structure rather than texture.
            .radius = (usePuff  ? Math_Mix(0.22f, 0.62f, powf(Random01(), 1.6f))
                                      * s_fvolBodySize
                       : useAtlas ? Math_Mix(0.30f, 0.46f, Random01())
                                  : Math_Mix(0.09f, 0.20f, powf(Random01(), 1.5f))) * scale,
            .lifetime = life,
            // alphaCurve uses colorStart.a as its multiplier even when the RGB
            // comes from a gradient. Keep the body contribution modest in BOTH
            // modes; otherwise alpha mode becomes an opaque patch and additive
            // mode washes a bright destination toward white.
            .colorStart = VC_WithAlpha(WHITE, (unsigned char)(255.0f * s_fvolBodyAlpha)),
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
            // Per-particle phase into the sheet. Sprites born in the same frame
            // otherwise hold the SAME frame for their whole lives — the flame
            // emits several per frame, so without this it reads as batches of
            // identical stamps rather than as many independent billows.
            .spriteAnimPhase = usePuff ? Random01() * FVOL_BODY_PHASE_MAX : 0.0f,
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
    emitter->lightTimer -= dtNow;
    if (emitter->lightTimer <= 0.0f)
    {
        emitter->lightTimer = 0.10f;
        float flick = 0.85f + VC_Flicker01(TimeFX_Elapsed() * 7.0f, emitter->seed) * 0.3f;
        // Lifted to mid-flame height. At the base the light sits IN the ground
        // plane, so the vector to it is nearly parallel to the floor and
        // dot(N, toL) collapses to ~0.1 — the ground receives almost nothing no
        // matter how bright the light is. Geometry first, intensity second.
        Vector3 lightPos = {pos.x, pos.y + 0.55f * scale, pos.z};
        VFXLight_Spawn(lightPos, (Color){255, 150, 60, 255},
                       4.0f * scale * flick, 0.13f, VFX_PRIORITY_LOW);
    }
}

int VFX_FlameEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float intensity)
{
    FVol_InitShared();
    int slot = -1;
    for (int i = 0; i < FVOL_MAX_EMITTERS; ++i)
        if (!s_fvolEmitters[i].active) { slot = i; break; }
    if (slot < 0) slot = s_fvolNextEmitter++ % FVOL_MAX_EMITTERS;
    unsigned int generation = s_fvolNextGeneration++;
    if (generation == 0) generation = s_fvolNextGeneration++;
    s_fvolEmitters[slot] = (VC_FlameEmitter){
        .active = true, .pos = pos, .matId = matId,
        .scale = scale > 0.0f ? scale : 1.0f,
        .intensity = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity),
        .legacyFeedAge = -1.0f,
        .seed = (float)slot * 1.6180339f + pos.x * 0.37f + pos.z * 0.71f,
        .generation = generation,
    };
    return slot;
}

void VFX_FlameEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].pos = pos;
    s_fvolEmitters[handle].wind = wind;
}

void VFX_FlameEmitter_SetIntensity(int handle, float intensity01)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].intensity = intensity01 < 0.0f ? 0.0f : (intensity01 > 1.0f ? 1.0f : intensity01);
}

void VFX_FlameEmitter_Stop(int handle)
{
    if (handle >= 0 && handle < FVOL_MAX_EMITTERS) s_fvolEmitters[handle].stopping = true;
}

void VFX_KillFlameEmitter(int handle)
{
    if (handle >= 0 && handle < FVOL_MAX_EMITTERS) s_fvolEmitters[handle].active = false;
}

static void VC_FlameEmitter_Update(float dt)
{
    for (int i = 0; i < FVOL_MAX_EMITTERS; ++i) {
        VC_FlameEmitter *emitter = &s_fvolEmitters[i];
        if (!emitter->active) continue;
        // The compatibility wrapper is frame-fed. Once its caller leaves the
        // draw/state path, release the slot rather than retaining one hidden
        // fire forever. Native handle emitters remain explicitly Stop/Kill.
        if (emitter->legacyFeedAge >= 0.0f) {
            emitter->legacyFeedAge += dt;
            if (emitter->legacyFeedAge > 0.25f) {
                emitter->active = false;
                continue;
            }
        }
        if (emitter->stopping) { emitter->active = false; continue; }
        FVol_Emit(emitter, dt);
    }
}

static void VC_FlameEmitter_Draw3D(Camera3D cam) { (void)cam; }

// Compatibility only. New code owns an emitter handle. This avoids a global
// accumulator while preserving old frame-fed callers until their P5 scores move.
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity)
{
    static int legacyHandle = -1;
    if (legacyHandle < 0 || !s_fvolEmitters[legacyHandle].active)
        legacyHandle = VFX_FlameEmitter_Spawn(pos, matId, scale, intensity);
    s_fvolEmitters[legacyHandle].legacyFeedAge = 0.0f;
    VFX_FlameEmitter_SetTransform(legacyHandle, pos, (Vector3){0});
    VFX_FlameEmitter_SetIntensity(legacyHandle, intensity);
}
