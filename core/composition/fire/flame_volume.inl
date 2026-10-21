// flame_volume.inl — Đợt E / F3. Fire rebuilt on the F1 lit-particle foundation
#include "core/motion/motion_recipe.h"
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
#define FVOL_MAX_VOLUME_LIVE 18

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
// Probability that a dying flame puff emits one independent smoke particle.
// Zero is clean fire; smoke never comes from the packed flame shader.
static float s_fvolSmokeAmt = 0.0f;
// Rise speed, as a multiplier on everything vertical. Meter-scale is unforgiving
// here: a 1 m flame whose particles travel 1-2 m in their 0.5 s lifetime reads as
// a blowtorch, not as fire — the embers outrun the flame that made them. Visible
// rise for a flame this size is more like 0.3-0.5 m over a particle's life.
static float s_fvolRiseMul = 1.45f;
// Base width. A flame is TALL and narrow; the ratio of base radius to rise
// height is what decides whether it reads as a flame or as a fireball.
static float s_fvolWidthMul = 0.50f;

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
static Texture2D s_fvolRoilTex = {0};    // UE5 Niagara 8x8 Fire Roil (fireroil_8x8.png)
static Texture2D s_fvolFireballTex = {0}; // UE5 Niagara 8x8 Fireball (fireball_8x8.png)
static Texture2D s_fvolTongue01Tex = {0}; // Taichi rising flame tongue 01 (fire_tongue_01_8x8.png)
static Texture2D s_fvolRoilNormalTex = {0};
static Texture2D s_fvolFireballNormalTex = {0};
static Texture2D s_fvolTongue01NormalTex = {0};
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
static float s_fvolBodyLive = 18.0f;
// Multiplier on the puff body's radius. Count and size buy the same cohesion at
// the same fill cost; size is the cheaper one in draw calls. Which is right is
// a look judgement, so both are tunables.
static float s_fvolBodySize = 1.05f;
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
static SpriteAnim s_fvolRoilAnim = {0};
static SpriteAnim s_fvolFireballAnim = {0};
static SpriteAnim s_fvolTongue01Anim = {0};
static float s_fvolVortexStrength = 1.5f;

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
static Texture2D s_fvolMotionTex = {0};
static float s_fvolMotionWarp = 1.0f;
static SpriteAnim s_fvolVolumeAnim = {0};
#include "pure_flame_puff_metadata.inl"
// Exposure on the sheet's emission before it indexes the ramp — "how much of
// the flame is white-hot". The sim normalises emission to its own 99.5th
// percentile and has no idea how bright this effect should read, so this is
// the knob that decides incandescent vs smouldering.
static float s_fvolHeatGain = 0.88f;
// Radiance gain on the flame half. SEPARATE from heatGain on purpose: heatGain
// moves the sprite along the ramp (what COLOUR it is), this moves how much light
// it throws (how BRIGHT it is).
static float s_fvolEmissive = 1.8f;
// Smoke is deliberately not decoded from the packed flame texture. Different
// fuels request it through flame_smoke_amount, which attaches a separate death
// sub-emitter with its own alpha blend, lighting, lifetime and wind response.
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
    ForceField fld;
    VFX_FlameStyle style;
    float vortexStrength;
} VC_FlameEmitter;

static VC_FlameEmitter s_fvolEmitters[FVOL_MAX_EMITTERS];
static int s_fvolNextEmitter = 0;
static unsigned int s_fvolNextGeneration = 1;

// Niagara NS_Fire dynamic force field: Swirling vortex along +Y around emitter base,
// thermal buoyancy rise, curl turbulence and atmospheric drag.
static void FVol_UpdateEmitterForce(VC_FlameEmitter *emitter)
{
    ForceField *f = &emitter->fld;
    f->layerCount = 0;
    // Layer 0: Buoyancy (upward thermal lift)
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = 1.0f,
    });
    // Layer 1: Niagara Swirling Vortex around flame center
    float vStr = (emitter->vortexStrength > 0.0f ? emitter->vortexStrength : s_fvolVortexStrength);
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_VORTEX,
        .origin = emitter->pos,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = 1.8f * vStr,
        .radius = 2.5f * (emitter->scale > 0.1f ? emitter->scale : 1.0f),
        .falloff = 1.0f,
    });
    // Layer 2: Curl Noise turbulence (lick ripples)
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_NOISE_CURL,
        .strength = 0.75f,
        .noiseScale = 2.2f / (emitter->scale > 0.1f ? emitter->scale : 1.0f),
        .noiseSpeed = 1.1f,
    });
    // Layer 3: Aerodynamic drag
    ForceField_AddLayer(f, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 1.8f,
    });
    // Layer 4: Ambient wind (if present)
    float windLen = Vector3Length(emitter->wind);
    if (windLen > 0.05f)
    {
        Vector3 windNorm = Vector3Scale(emitter->wind, 1.0f / windLen);
        ForceField_AddLayer(f, (ForceLayer){
            .type = FORCE_WIND,
            .direction = windNorm,
            .strength = windLen,
        });
    }
}

static void FVol_InitShared(void)
{
    if (s_fvolInit)
        return;

    Tuning_RegisterFloat("flame_core_alpha", &s_fvolCoreAlpha, 0.55f);
    Tuning_RegisterFloat("flame_smoke_amount", &s_fvolSmokeAmt, 0.0f);
    Tuning_RegisterFloat("flame_rise_mul", &s_fvolRiseMul, 1.45f);
    Tuning_RegisterFloat("flame_width_mul", &s_fvolWidthMul, 0.50f);
    Tuning_RegisterFloat("flame_atlas", &s_fvolAtlas, 1.0f); // 0 sprites/1 puff/2 column
    Tuning_RegisterFloat("flame_body_count", &s_fvolBodyCount, 1.0f);
    Tuning_RegisterFloat("flame_body_live", &s_fvolBodyLive, 18.0f);
    Tuning_RegisterFloat("flame_body_size", &s_fvolBodySize, 1.05f);
    Tuning_RegisterFloat("flame_spread", &s_fvolSpread, 1.0f);
    Tuning_RegisterFloat("flame_body_blend", &s_fvolBodyBlend, 0.0f);
    /* The default here WINS over the static initialiser above — Tuning_RegisterFloat
       assigns it. Changing one without the other is a silent no-op, and was. */
    Tuning_RegisterFloat("flame_body_alpha", &s_fvolBodyAlpha, 0.35f);
    Tuning_RegisterFloat("flame_volume", &s_fvolVolume, 1.0f);
    Tuning_RegisterFloat("flame_motion_warp", &s_fvolMotionWarp, 1.0f);
    Tuning_RegisterFloat("flame_heat_gain", &s_fvolHeatGain, 0.88f);
    Tuning_RegisterFloat("flame_emissive", &s_fvolEmissive, 1.8f);
    Tuning_RegisterFloat("flame_vortex_strength", &s_fvolVortexStrength, 1.5f);

    // The packed VOLUME sheet — the same sim as the split above, delivered with
    // its temperature field intact. Missing file falls through to the legacy
    // path rather than drawing nothing.
    const VFX_SurfaceProfile *volProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_VOLUME);
    s_fvolVolumeTex = volProfile != NULL ? volProfile->body : (Texture2D){0};
    s_fvolMotionTex = volProfile != NULL ? volProfile->flowMap : (Texture2D){0};
    if (s_fvolMotionTex.id != 0)
    {
        SetTextureFilter(s_fvolMotionTex, TEXTURE_FILTER_BILINEAR);
    }
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
        SpriteAnim_SetFrameMetadata(&s_fvolVolumeAnim, s_fvolPureFlamePuffFrameMeta,
                                    (int)(sizeof(s_fvolPureFlamePuffFrameMeta) /
                                          sizeof(s_fvolPureFlamePuffFrameMeta[0])));
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

    // Extracted Niagara sheets still go through the semantic registry: the
    // composition owns behaviour, while the manifest owns paths and sampling.
    const VFX_SurfaceProfile *roilProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_ROIL_NIAGARA);
    s_fvolRoilTex = roilProfile != NULL ? roilProfile->body : (Texture2D){0};
    s_fvolRoilNormalTex = roilProfile != NULL ? roilProfile->normalMap : (Texture2D){0};
    if (s_fvolRoilTex.id != 0)
    {
        SetTextureFilter(s_fvolRoilTex, TEXTURE_FILTER_BILINEAR);
        SpriteAnim_Init(&s_fvolRoilAnim, 8, 8, 64,
                        64.0f / (FVOL_BODY_LIFE_MAX + FVOL_BODY_PHASE_MAX),
                        ANIM_ONCE);
    }

    const VFX_SurfaceProfile *fireballProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIREBALL_NIAGARA);
    s_fvolFireballTex = fireballProfile != NULL ? fireballProfile->body : (Texture2D){0};
    s_fvolFireballNormalTex = fireballProfile != NULL ? fireballProfile->normalMap : (Texture2D){0};
    if (s_fvolFireballTex.id != 0)
    {
        SetTextureFilter(s_fvolFireballTex, TEXTURE_FILTER_BILINEAR);
        SpriteAnim_Init(&s_fvolFireballAnim, 8, 8, 64,
                        64.0f / (FVOL_BODY_LIFE_MAX + FVOL_BODY_PHASE_MAX),
                        ANIM_ONCE);
    }

    const VFX_SurfaceProfile *tongue01Profile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_FIRE_TONGUE_01);
    s_fvolTongue01Tex = tongue01Profile != NULL ? tongue01Profile->body : (Texture2D){0};
    s_fvolTongue01NormalTex = tongue01Profile != NULL ? tongue01Profile->normalMap : (Texture2D){0};
    if (s_fvolTongue01Tex.id != 0)
    {
        SetTextureFilter(s_fvolTongue01Tex, TEXTURE_FILTER_BILINEAR);
        SpriteAnim_Init(&s_fvolTongue01Anim, 8, 8, 64, 64.0f / 1.7f, ANIM_ONCE);
    }

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
        // Authored vibrant radiant fire (Ghost of Tsushima Planck Blackbody progression):
        // 900K soot/ruby -> 1600K crimson -> 2200K fiery scarlet -> 2800K cadmium orange -> 3400K golden amber -> 4200K+ incandescent white-hot core
        ColorGradient_AddStop(g, 0.00f, (Color){45, 5, 2, 255});       // glowing deep ruby wisp / soot
        ColorGradient_AddStop(g, 0.12f, (Color){165, 15, 2, 255});     // radiant crimson tongue
        ColorGradient_AddStop(g, 0.30f, (Color){235, 50, 4, 255});     // fiery scarlet
        ColorGradient_AddStop(g, 0.55f, (Color){255, 120, 8, 255});    // vivid cadmium fiery orange
        ColorGradient_AddStop(g, 0.78f, (Color){255, 175, 18, 255});   // radiant amber gold
        ColorGradient_AddStop(g, 0.90f, (Color){255, 225, 55, 255});   // intense warm core
        ColorGradient_AddStop(g, 0.97f, (Color){255, 250, 180, 255});  // incandescent core
        ColorGradient_AddStop(g, 1.00f, (Color){255, 255, 245, 255});  // white-hot peak (Planck high-energy)
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
    // Resolve flame style
    VFX_FlameStyle style = emitter->style;
    if (style == VFX_FLAME_STYLE_DEFAULT)
    {
        if (s_fvolRoilTex.id != 0)
            style = VFX_FLAME_STYLE_NIAGARA_ROIL;
        else if (s_fvolVolume > 0.5f && s_fvolVolumeTex.id != 0)
            style = VFX_FLAME_STYLE_VOLUME;
        else if (s_fvolAtlas >= 1.5f)
            style = VFX_FLAME_STYLE_COLUMN;
        else if (s_fvolAtlas >= 0.5f)
            style = VFX_FLAME_STYLE_PUFF;
        else
            style = VFX_FLAME_STYLE_VOLUME;
    }

    const bool isRoil = (style == VFX_FLAME_STYLE_NIAGARA_ROIL) || (style == VFX_FLAME_STYLE_DEFAULT && s_fvolRoilTex.id != 0);
    const bool isFireball = (style == VFX_FLAME_STYLE_FIREBALL);
    const bool isVol = (style == VFX_FLAME_STYLE_VOLUME);
    const bool isTongue01 = (style == VFX_FLAME_STYLE_FIRE_TONGUE_01);

    // Any UE EOO packed texture (Roil, Fireball, Volume, Tongue01) uses the GPU Volume Path (volumeSheet = 2/4 + rampLUT)
    const bool useVolume = (s_fvolVolume > 0.5f) &&
                           ((isRoil && s_fvolRoilTex.id != 0) ||
                            (isFireball && s_fvolFireballTex.id != 0) ||
                            (isTongue01 && s_fvolTongue01Tex.id != 0) ||
                            (isVol && s_fvolVolumeTex.id != 0) ||
                            (style == VFX_FLAME_STYLE_DEFAULT && s_fvolVolumeTex.id != 0));

    const bool wantPuff = (style == VFX_FLAME_STYLE_PUFF) || (!useVolume && !isRoil && !isFireball && !isTongue01 && (s_fvolAtlas > 0.5f) && (s_fvolAtlas < 1.5f));
    const bool usePuff = wantPuff && (s_fvolPuffTex.id != 0);
    const bool useRoil = isRoil && (s_fvolRoilTex.id != 0);
    const bool useFireball = isFireball && (s_fvolFireballTex.id != 0);
    const bool useTongue01 = isTongue01 && (s_fvolTongue01Tex.id != 0);
    const bool useAtlas = (s_fvolAtlas > 0.5f || !useVolume)
                          && (useRoil || useFireball || usePuff || useTongue01 || s_fvolFlameTex.id != 0);

    Texture2D bodyTex = s_fvolFlameTex;
    SpriteAnim *bodyAnim = &s_fvolFlameAnim;
    if (useRoil)
    {
        bodyTex = s_fvolRoilTex;
        bodyAnim = &s_fvolRoilAnim;
    }
    else if (useFireball)
    {
        bodyTex = s_fvolFireballTex;
        bodyAnim = &s_fvolFireballAnim;
    }
    else if (useTongue01)
    {
        bodyTex = s_fvolTongue01Tex;
        bodyAnim = &s_fvolTongue01Anim;
    }
    else if (usePuff)
    {
        bodyTex = s_fvolPuffTex;
        bodyAnim = &s_fvolPuffAnim;
    }

    // Dynamic Niagara Vortex force field per emitter instance
    const ForceField *activeField = (emitter->fld.layerCount > 0) ? &emitter->fld : &s_fvolFld;

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
        // Each atlas particle is a complete simulated flame puff. Letting a hot
        // tuning value stack dozens of them drives the premultiplied centre to
        // opaque white and destroys the baked internal structure. Keep this a
        // small whole-puff population; intensity may only reduce the budget.
        const float requestedLive = s_fvolBodyLive * intensity * s_fvolBodyCount;
        const float live = fminf(requestedLive, FVOL_MAX_VOLUME_LIVE);
        *bodyAccum += dtNow * (live / FVOL_BODY_LIFE_AVG);
        int n = (int)*bodyAccum;
        *bodyAccum -= (float)n;
        if (n > 24) { n = 24; *bodyAccum = 0.0f; }
        if (n < 0) n = 0;

        const Texture2D ramp = FVol_RampLUT(matId);
        Texture2D chosenVolTex = s_fvolVolumeTex;
        SpriteAnim *chosenVolAnim = &s_fvolVolumeAnim;
        if (useRoil && s_fvolRoilTex.id != 0)
        {
            chosenVolTex = s_fvolRoilTex;
            chosenVolAnim = &s_fvolRoilAnim;
        }
        else if (useFireball && s_fvolFireballTex.id != 0)
        {
            chosenVolTex = s_fvolFireballTex;
            chosenVolAnim = &s_fvolFireballAnim;
        }
        else if (useTongue01 && s_fvolTongue01Tex.id != 0)
        {
            chosenVolTex = s_fvolTongue01Tex;
            chosenVolAnim = &s_fvolTongue01Anim;
        }

        for (int i = 0; i < n; i++)
        {
            float ang = Random01() * 2.0f * PI;
            // The volume sheet is one billow, therefore its emitter is the
            // silhouette author. A compact foot plus a longer vertical travel
            // reads as flame; the old 0.34 m disk made a wide fireball even
            // when the asset itself was completely directionless.
            // Compact foot plus a longer vertical travel gives a tapered flame cone.
            float rad = sqrtf(Random01()) * 0.18f * s_fvolSpread * scale * s_fvolWidthMul;
            Vector3 p = {pos.x + cosf(ang) * rad,
                         pos.y + Random01() * 0.05f * scale,
                         pos.z + sinf(ang) * rad};
            float life = Math_Mix(0.80f, FVOL_BODY_LIFE_MAX, Random01());

            // Smoke is a separate material population. ParticleSystem copies
            // this config into the parent at spawn, then places the child at the
            // parent's death position; no packed soot/opacity enters flame RGB.
            float smokeChance = s_fvolSmokeAmt;
            if (smokeChance < 0.0f) smokeChance = 0.0f;
            else if (smokeChance > 1.0f) smokeChance = 1.0f;
            bool emitSmoke = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS].id != 0 &&
                             Random01() < smokeChance;
            s_fvolSmokeSeed = (ParticleConfig){
                .velocity = {0.0f, 0.18f * scale, 0.0f},
                .velocityInheritance = 0.12f,
                .radius = Math_Mix(0.16f, 0.25f, Random01()) * scale,
                .lifetime = Math_Mix(1.0f, 1.6f, Random01()),
                .colorStart = (Color){220, 220, 220, 72},
                .colorEnd = (Color){190, 190, 190, 0},
                .forceField = &s_smokePuffFld,
                .windInfluence = 0.85f,
                .radiusCurve = &s_smokePuffGrow,
                .alphaCurve = &s_smokePuffFade,
                .render.texture = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
                .render.sixWayLighting = 1,
                .render.sixWayScattering = 1.25f,
                .render.sixWayAbsorption = 1.10f,
                .rotation = Random01() * 2.0f * PI,
                .angularVelocity = (Random01() - 0.5f) * 0.35f,
            };

            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = {cosf(ang) * 0.03f * scale,
                             Math_Mix(0.70f, 1.05f, Random01()) * scale * s_fvolRiseMul,
                             sinf(ang) * 0.03f * scale},
                .radius = Math_Mix(0.16f, 0.36f, powf(Random01(), 1.2f))
                          * s_fvolBodySize * scale * (useRoil ? 1.45f : (useTongue01 ? 1.35f : 1.0f)),
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
                .forceField = activeField,
                .windInfluence = 0.65f,
                .render.volumeSheet = (useRoil || useFireball || useTongue01) ? 4 : 2,
                .render.normalTex = useTongue01 ? s_fvolTongue01NormalTex : (useRoil ? s_fvolRoilNormalTex : (useFireball ? s_fvolFireballNormalTex : (Texture2D){0})),
                .render.rampLUT = ramp,
                .render.heatGain = s_fvolHeatGain,
                .render.emissiveBoost = s_fvolEmissive,
                .render.smokeGain = 0.0f,
                .render.motionTex = s_fvolMotionTex,
                .render.motionWarpScale = s_fvolMotionWarp,
                .render.sixWayLighting = 1,
                .render.sixWayScattering = 1.35f,
                .render.sixWayAbsorption = 1.20f,
                .render.blendMode = VFX_BLEND_PREMULTIPLIED,
                .render.texture = (chosenVolTex.id != 0) ? chosenVolTex : s_fvolVolumeTex,
                .spriteAnim = (chosenVolAnim != NULL) ? chosenVolAnim : &s_fvolVolumeAnim,
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
                .stretchStrength = 0.45f,
                .stretchMinSpeed = 0.10f,
                .rotation = (Random01() - 0.5f) * 0.35f,
                .angularVelocity = (Random01() - 0.5f) * 0.15f,
                .onDeathEmit = emitSmoke ? &s_fvolSmokeSeed : NULL,
                .onDeathEmitCount = emitSmoke ? 1 : 0,
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
            .windInfluence = 0.85f,
            .radiusCurve = &s_smokePuffGrow,
            .render.texture = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .render.sixWayLighting = 1,
            .render.sixWayScattering = 1.25f,
            .render.sixWayAbsorption = 1.10f,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.7f,
        };
        s_fvolSmokeSeed = smoke;

        ParticleConfig cfg = {
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
            .radius = (useRoil  ? Math_Mix(0.35f, 0.65f, powf(Random01(), 1.3f)) * s_fvolBodySize
                       : useFireball ? Math_Mix(0.30f, 0.55f, powf(Random01(), 1.2f)) * s_fvolBodySize
                       : useTongue01 ? Math_Mix(0.30f, 0.55f, Random01()) * s_fvolBodySize
                       : usePuff  ? Math_Mix(0.22f, 0.62f, powf(Random01(), 1.6f)) * s_fvolBodySize
                       : useAtlas ? Math_Mix(0.30f, 0.46f, Random01())
                                  : Math_Mix(0.09f, 0.20f, powf(Random01(), 1.5f))) * scale,
            .lifetime = life,
            // alphaCurve uses colorStart.a as its multiplier even when the RGB
            // comes from a gradient. Keep the body contribution modest in BOTH
            // modes; otherwise alpha mode becomes an opaque patch and additive
            // mode washes a bright destination toward white.
            .colorStart = VC_WithAlpha(WHITE, (unsigned char)(255.0f * (useRoil ? 0.85f : s_fvolBodyAlpha))),
            .colorEnd = (Color){44, 40, 38, 0},
            .gradient = (useRoil && matId == VC_MAT_FIRE) ? NULL : &s_fvolBodyGrad,
            .forceField = activeField,
            .windInfluence = 0.65f,
            .radiusCurve = &s_fvolGrow,
            .alphaCurve = &s_fvolFade,
            .speedCurve = &s_fvolRise,
            .render.blendMode = (useRoil || useFireball) ? VFX_BLEND_PREMULTIPLIED : ((useAtlas && s_fvolBodyBlend > 0.5f) ? VFX_BLEND_ADDITIVE : VFX_BLEND_ALPHA),
            .render.volumeSheet = (useRoil || useFireball) ? 4 : 0,
            .render.rampLUT = (useRoil || useFireball) ? FVol_RampLUT(matId) : (Texture2D){0},
            .render.normalTex = useRoil ? s_fvolRoilNormalTex
                                        : (useFireball ? s_fvolFireballNormalTex : (Texture2D){0}),
            .render.heatGain = (useRoil || useFireball) ? s_fvolHeatGain : 1.0f,
            // Additive accumulates, so each sprite must contribute LESS or a few
            // overlapping tongues clip straight to white.
            .render.emissiveBoost = useRoil ? s_fvolEmissive : ((useAtlas && s_fvolBodyBlend > 0.5f) ? 1.05f : 1.0f),
            .render.texture = useAtlas ? bodyTex
                                       : s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .spriteAnim = useAtlas ? bodyAnim : NULL,
            // Per-particle phase into the sheet. Sprites born in the same frame
            // otherwise hold the SAME frame for their whole lives — the flame
            // emits several per frame, so without this it reads as batches of
            // identical stamps rather than as many independent billows.
            .spriteAnimPhase = (usePuff || useRoil || useFireball) ? Random01() * FVOL_BODY_PHASE_MAX : 0.0f,
            // FIRE EMITS LIGHT — it must not be multiplied by the scene's.
            // Lighting is a multiply, so a flame lit by a dim sky turns brown;
            // that is what "the fire went black" was. Only its smoke is lit.
            .render.unlit = (useRoil || useFireball) ? 0 : 1,
            // Rotation is a property of the SHEET, not of the atlas path. The
            // COLUMN sheet has an UP — spinning it renders the fire upside down
            // (the bug that hid the engine's flipped-quad landmine for so long).
            // The PUFF/ROIL sheet was simulated with isotropic turbulence, so
            // spinning it keeps ten sprites off one sheet from reading identical.
            .rotation = (!useAtlas || usePuff || useRoil || useFireball) ? Random01() * 2.0f * PI : 0.0f,
            .angularVelocity = (!useAtlas || usePuff || useRoil || useFireball)
                                   ? (Random01() - 0.5f) * (usePuff ? 0.5f : (useRoil ? 0.7f : 1.4f))
                                   : 0.0f,
        };
        if (useTongue01)
        {
            cfg.render.blendMode = VFX_BLEND_PREMULTIPLIED;
            cfg.render.volumeSheet = 2;
            cfg.render.rampLUT = FVol_RampLUT(matId);
            cfg.render.normalTex = s_fvolTongue01NormalTex;
            cfg.render.heatGain = s_fvolHeatGain;
            cfg.render.emissiveBoost = s_fvolEmissive;
            cfg.render.unlit = 0;
            cfg.rotation = (Random01() - 0.5f) * 0.20f;
            cfg.angularVelocity = 0.0f;
        }
        SpawnParticle(cfg);
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
            .forceField = activeField,
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

int VFX_FlameEmitter_SpawnEx(Vector3 pos, VC_MaterialId matId, float scale, float intensity, VFX_FlameStyle style)
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
        .style = style,
        .vortexStrength = s_fvolVortexStrength,
    };
    FVol_UpdateEmitterForce(&s_fvolEmitters[slot]);
    return slot;
}

int VFX_FlameEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float intensity)
{
    return VFX_FlameEmitter_SpawnEx(pos, matId, scale, intensity, VFX_FLAME_STYLE_DEFAULT);
}

void VFX_FlameEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].pos = pos;
    s_fvolEmitters[handle].wind = wind;
    FVol_UpdateEmitterForce(&s_fvolEmitters[handle]);
}

void VFX_FlameEmitter_SetIntensity(int handle, float intensity01)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].intensity = intensity01 < 0.0f ? 0.0f : (intensity01 > 1.0f ? 1.0f : intensity01);
}

void VFX_FlameEmitter_SetStyle(int handle, VFX_FlameStyle style)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].style = style;
}

void VFX_FlameEmitter_SetVortex(int handle, float strength)
{
    if (handle < 0 || handle >= FVOL_MAX_EMITTERS || !s_fvolEmitters[handle].active) return;
    s_fvolEmitters[handle].vortexStrength = strength;
    FVol_UpdateEmitterForce(&s_fvolEmitters[handle]);
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

const char *VFX_FlameStyle_Name(VFX_FlameStyle style)
{
    static const char *const names[6] = {
        "NIAGARA ROIL", "VOLUME PUFF", "COLUMN", "PUFF", "FIREBALL", "FIRE TONGUE 01"
    };
    int s = (int)style;
    if (s < 0 || s >= 6) s = 0;
    return names[s];
}

void VFX_ComposeAmbientFireEx(Vector3 pos, VC_MaterialId matId, float scale, float intensity, VFX_FlameStyle style)
{
    static int legacyHandle = -1;
    if (legacyHandle < 0 || !s_fvolEmitters[legacyHandle].active)
        legacyHandle = VFX_FlameEmitter_SpawnEx(pos, matId, scale, intensity, style);
    s_fvolEmitters[legacyHandle].legacyFeedAge = 0.0f;
    s_fvolEmitters[legacyHandle].style = style;
    VFX_FlameEmitter_SetTransform(legacyHandle, pos, (Vector3){0});
    VFX_FlameEmitter_SetIntensity(legacyHandle, intensity);
}

// Unified / Generalized Public APIs
void VFX_ComposeFlame(Vector3 pos, VC_MaterialId matId, float scale, float intensity)
{
    (void)matId;
    (void)scale;
    (void)intensity;
    VFX_ComposeFireEx(pos, VFX_Fire_GetLiveConfig());
}

void VFX_ComposeAmbientFire(Vector3 pos, VC_MaterialId matId, float scale, float intensity)
{
    (void)matId;
    (void)scale;
    (void)intensity;
    VFX_ComposeFireEx(pos, VFX_Fire_GetLiveConfig());
}

// ============================================================================
// UNIFIED MODERN FIRE SYSTEM (4 MODES + MOTION FIELD GUIDANCE)
// ============================================================================

static Texture2D s_fvolProceduralNoiseTex = {0};

static void FVol_EnsureProceduralNoise(void)
{
    if (s_fvolProceduralNoiseTex.id != 0) return;
    const int S = 256;
    unsigned char *data = (unsigned char *)malloc(S * S);
    if (!data) return;

    // Exact 4-octave seamless multi-frequency noise matching WebGL Fire VFX 3D.html
    const int P_vals[4] = {8, 16, 32, 64};
    float *octaves[4];
    unsigned int rng = 1234567;
    for (int o = 0; o < 4; ++o) {
        int P = P_vals[o];
        octaves[o] = (float *)malloc(P * P * sizeof(float));
        if (!octaves[o]) {
            for (int k = 0; k < o; ++k) free(octaves[k]);
            free(data);
            return;
        }
        for (int i = 0; i < P * P; ++i) {
            rng = (rng * 1664525u + 1013904223u);
            octaves[o][i] = (float)rng / 4294967296.0f;
        }
    }
    for (int j = 0; j < S; ++j) {
        for (int i = 0; i < S; ++i) {
            float v = 0.0f, a = 0.5f;
            for (int o = 0; o < 4; ++o) {
                int P = P_vals[o];
                float fx = (float)i / (float)S * (float)P;
                float fy = (float)j / (float)S * (float)P;
                int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
                float tx = fx - (float)x0, ty = fy - (float)y0;
                tx = tx * tx * (3.0f - 2.0f * tx);
                ty = ty * ty * (3.0f - 2.0f * ty);
                int x1 = (x0 + 1) % P, y1 = (y0 + 1) % P;
                int xa = x0 % P, ya = y0 % P;
                float a0 = octaves[o][ya * P + xa], b0 = octaves[o][ya * P + x1];
                float c0 = octaves[o][y1 * P + xa], d0 = octaves[o][y1 * P + x1];
                v += ((a0 + (b0 - a0) * tx) * (1.0f - ty) + (c0 + (d0 - c0) * tx) * ty) * a;
                a *= 0.5f;
            }
            float val = (v / 0.9375f) * 255.0f;
            data[j * S + i] = (unsigned char)(val > 255.0f ? 255 : (val < 0.0f ? 0 : val));
        }
    }
    for (int o = 0; o < 4; ++o) free(octaves[o]);

    Image img = {
        .data = data,
        .width = S,
        .height = S,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE
    };
    s_fvolProceduralNoiseTex = LoadTextureFromImage(img);
    SetTextureWrap(s_fvolProceduralNoiseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(s_fvolProceduralNoiseTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
}

static VFX_FireConfig s_liveFireConfig = {
    .preset = VFX_FIRE_PRESET_TORCH,
    .mode = VFX_FIRE_MODE_SHADER_TONGUE,
    .emitterShape = VFX_FIRE_EMITTER_POINT,
    .emissionMode = VFX_FIRE_EMIT_STEADY,
    .motionType = VFX_FIRE_MOTION_AIRFLOW,
    .attachment = VFX_FIRE_ATTACH_ANCHORED,
    .sourceRadius = 0.12f,
    .sourceLength = 2.0f,
    .fieldRadius = 1.0f,
    .fieldSpeed = 2.4f,
    .swirlSpeed = 2.0f,
    .turbulenceSpeed = 1.8f,
    .buoyancy = 4.8f,
    .particleCount = 50,
    .lifetime = 1.5f,
    .particleRadius = 0.22f,
    .riseMultiplier = 1.4f,
    .emitDuration = 0.0f,
    .intensity = 2.0f,
    .coreHeat = 1.25f,
    .distortion = 0.40f,
    .softness = 0.50f,
    .emissiveBoost = 1.0f,
    .flickerRate = 14.0f,
    .emitSparks = true,
    .emitSmoke = false,
    .dynamicLight = true
};

VFX_FireConfig VFX_Fire_DefaultConfig(void)
{
    return s_liveFireConfig;
}

VFX_FireConfig *VFX_Fire_GetLiveConfig(void)
{
    return &s_liveFireConfig;
}

void VFX_Fire_ApplyPreset(VFX_FireConfig *cfg, VFX_FirePreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;
    switch (preset) {
    case VFX_FIRE_PRESET_TORCH:
        cfg->emitterShape = VFX_FIRE_EMITTER_POINT;
        cfg->emissionMode = VFX_FIRE_EMIT_STEADY;
        cfg->motionType = VFX_FIRE_MOTION_AIRFLOW;
        cfg->attachment = VFX_FIRE_ATTACH_ANCHORED;
        cfg->mode = VFX_FIRE_MODE_SHADER_TONGUE;
        cfg->sourceRadius = 0.08f;
        cfg->particleCount = 50;
        cfg->particleRadius = 0.20f;
        cfg->buoyancy = 4.5f;
        cfg->swirlSpeed = 1.5f;
        cfg->turbulenceSpeed = 1.8f;
        cfg->coreHeat = 1.30f;
        cfg->intensity = 2.0f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_CAMPFIRE:
        cfg->emitterShape = VFX_FIRE_EMITTER_CIRCLE;
        cfg->emissionMode = VFX_FIRE_EMIT_PULSING;
        cfg->motionType = VFX_FIRE_MOTION_AIRFLOW;
        cfg->attachment = VFX_FIRE_ATTACH_ANCHORED;
        cfg->mode = VFX_FIRE_MODE_SHADER_TONGUE;
        cfg->sourceRadius = 0.45f;
        cfg->particleCount = 120;
        cfg->particleRadius = 0.26f;
        cfg->buoyancy = 5.2f;
        cfg->swirlSpeed = 2.5f;
        cfg->turbulenceSpeed = 2.4f;
        cfg->coreHeat = 1.20f;
        cfg->intensity = 2.2f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_FIRE_TORNADO:
        cfg->emitterShape = VFX_FIRE_EMITTER_CIRCLE;
        cfg->emissionMode = VFX_FIRE_EMIT_STEADY;
        cfg->motionType = VFX_FIRE_MOTION_VORTEX;
        cfg->attachment = VFX_FIRE_ATTACH_ANCHORED;
        cfg->mode = VFX_FIRE_MODE_SHADER_TONGUE;
        cfg->sourceRadius = 0.75f;
        cfg->particleCount = 350;
        cfg->particleRadius = 0.35f;
        cfg->buoyancy = 12.0f;
        cfg->swirlSpeed = 18.0f;
        cfg->turbulenceSpeed = 4.5f;
        cfg->coreHeat = 1.60f;
        cfg->intensity = 2.5f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_FLAMETHROWER:
        cfg->emitterShape = VFX_FIRE_EMITTER_POINT;
        cfg->emissionMode = VFX_FIRE_EMIT_SPRAY;
        cfg->motionType = VFX_FIRE_MOTION_JET;
        cfg->attachment = VFX_FIRE_ATTACH_FREE;
        cfg->mode = VFX_FIRE_MODE_SHADER_PUFF;
        cfg->sourceRadius = 0.10f;
        cfg->particleCount = 220;
        cfg->particleRadius = 0.32f;
        cfg->fieldSpeed = 14.0f;
        cfg->buoyancy = 1.2f;
        cfg->swirlSpeed = 1.0f;
        cfg->distortion = 0.55f;
        cfg->intensity = 2.2f;
        cfg->emitSparks = false;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_FIREBALL:
        cfg->emitterShape = VFX_FIRE_EMITTER_SPHERE;
        cfg->emissionMode = VFX_FIRE_EMIT_BURST;
        cfg->motionType = VFX_FIRE_MOTION_BLAST;
        cfg->attachment = VFX_FIRE_ATTACH_FREE;
        cfg->mode = VFX_FIRE_MODE_SHADER_PUFF;
        cfg->sourceRadius = 0.45f;
        cfg->particleCount = 180;
        cfg->particleRadius = 0.35f;
        cfg->fieldSpeed = 8.0f;
        cfg->distortion = 0.60f;
        cfg->intensity = 3.0f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_FIRE_WALL:
        cfg->emitterShape = VFX_FIRE_EMITTER_LINE;
        cfg->emissionMode = VFX_FIRE_EMIT_STEADY;
        cfg->motionType = VFX_FIRE_MOTION_AIRFLOW;
        cfg->attachment = VFX_FIRE_ATTACH_ANCHORED;
        cfg->mode = VFX_FIRE_MODE_SHADER_TONGUE;
        cfg->sourceLength = 4.0f;
        cfg->particleCount = 240;
        cfg->particleRadius = 0.28f;
        cfg->buoyancy = 6.0f;
        cfg->swirlSpeed = 2.0f;
        cfg->turbulenceSpeed = 2.2f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    case VFX_FIRE_PRESET_GROUND_FIRE:
        cfg->emitterShape = VFX_FIRE_EMITTER_CIRCLE;
        cfg->emissionMode = VFX_FIRE_EMIT_PULSING;
        cfg->motionType = VFX_FIRE_MOTION_AIRFLOW;
        cfg->attachment = VFX_FIRE_ATTACH_SURFACE_CLING;
        cfg->mode = VFX_FIRE_MODE_SHADER_TONGUE;
        cfg->sourceRadius = 1.8f;
        cfg->particleCount = 160;
        cfg->particleRadius = 0.25f;
        cfg->buoyancy = 3.0f;
        cfg->swirlSpeed = 1.0f;
        cfg->turbulenceSpeed = 2.5f;
        cfg->emitSparks = true;
        cfg->dynamicLight = true;
        break;
    default:
        break;
    }
}

const char *VFX_FirePreset_Name(VFX_FirePreset preset)
{
    static const char *const names[VFX_FIRE_PRESET_COUNT] = {
        "Custom", "Torch", "Campfire", "Fire Tornado", "Flamethrower", "Fireball", "Fire Wall", "Ground Fire"
    };
    int p = (int)preset;
    if (p < 0 || p >= VFX_FIRE_PRESET_COUNT) p = 0;
    return names[p];
}

const char *VFX_FireMode_Name(VFX_FireMode mode)
{
    static const char *const names[VFX_FIRE_MODE_COUNT] = {
        "Shader Tongue", "Shader Puff", "Flipbook Tongue", "Flipbook Puff"
    };
    int m = (int)mode;
    if (m < 0 || m >= VFX_FIRE_MODE_COUNT) m = 0;
    return names[m];
}

const char *VFX_FireEmitterShape_Name(VFX_FireEmitterShape shape)
{
    static const char *const names[VFX_FIRE_EMITTER_COUNT] = {
        "Point", "Circle (Disc)", "Sphere (3D)", "Line (Wall)", "Ring", "Hemisphere"
    };
    int s = (int)shape;
    if (s < 0 || s >= VFX_FIRE_EMITTER_COUNT) s = 0;
    return names[s];
}

const char *VFX_FireEmissionMode_Name(VFX_FireEmissionMode mode)
{
    static const char *const names[VFX_FIRE_EMIT_COUNT] = {
        "Steady", "Pulsing (Breathing)", "Burst (Detonation)", "Spray (Jet)"
    };
    int e = (int)mode;
    if (e < 0 || e >= VFX_FIRE_EMIT_COUNT) e = 0;
    return names[e];
}

const char *VFX_FireMotion_Name(VFX_FireMotionType motion)
{
    static const char *const names[VFX_FIRE_MOTION_COUNT] = {
        "Airflow (Upward)", "Vortex (Whirlwind)", "Blast (Radial)", "Jet (Forward)", "Inward (Suction)"
    };
    int m = (int)motion;
    if (m < 0 || m >= VFX_FIRE_MOTION_COUNT) m = 0;
    return names[m];
}

const char *VFX_FireAttachment_Name(VFX_FireAttachment attachment)
{
    static const char *const names[VFX_FIRE_ATTACH_COUNT] = {
        "Anchored (Attached)", "Free (Ballistic)", "Surface Cling (Ground)"
    };
    int a = (int)attachment;
    if (a < 0 || a >= VFX_FIRE_ATTACH_COUNT) a = 0;
    return names[a];
}

int VFX_Fire_GetParams(VFX_FireConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *const presetNames[] = {
        "Custom", "Torch", "Campfire", "Fire Tornado", "Flamethrower", "Fireball", "Fire Wall", "Ground Fire"
    };
    static const char *const modeNames[] = {
        "Shader Tongue", "Shader Puff", "Flipbook Tongue", "Flipbook Puff"
    };
    static const char *const shapeNames[] = {
        "Point", "Circle (Disc)", "Sphere (3D)", "Line (Wall)", "Ring", "Hemisphere"
    };
    static const char *const emitNames[] = {
        "Steady", "Pulsing (Breathing)", "Burst (Detonation)", "Spray (Jet)"
    };
    static const char *const motionNames[] = {
        "Airflow (Upward)", "Vortex (Whirlwind)", "Blast (Radial)", "Jet (Forward)", "Inward (Suction)"
    };
    static const char *const attachNames[] = {
        "Anchored (Attached)", "Free (Ballistic)", "Surface Cling (Ground)"
    };

#define FIRE_PARAM_ENUM(pname, pgroup, val, enames, ecount) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = (ecount) - 1, \
        .stepFloat = 1.0f, .enumNames = enames, .enumCount = ecount \
    }; } while(0)

#define FIRE_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define FIRE_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define FIRE_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    // Preset & Architecture
    FIRE_PARAM_ENUM("Preset", "Preset", cfg->preset, presetNames, VFX_FIRE_PRESET_COUNT);
    FIRE_PARAM_ENUM("Fire Mode", "Preset", cfg->mode, modeNames, VFX_FIRE_MODE_COUNT);
    FIRE_PARAM_ENUM("Attachment", "Preset", cfg->attachment, attachNames, VFX_FIRE_ATTACH_COUNT);

    // Particle Configuration & Scale
    FIRE_PARAM_FLOAT("Particle size m", "Particle", cfg->particleRadius, 0.05f, 3.0f, 0.05f);
    FIRE_PARAM_INT("Particle count", "Particle", cfg->particleCount, 5, 1000);
    FIRE_PARAM_FLOAT("Lifetime s", "Particle", cfg->lifetime, 0.2f, 5.0f, 0.1f);
    FIRE_PARAM_FLOAT("Rise multiplier", "Particle", cfg->riseMultiplier, 0.2f, 5.0f, 0.1f);
    FIRE_PARAM_FLOAT("Emission time s", "Particle", cfg->emitDuration, 0.0f, 10.0f, 0.5f);

    // Emitter Geometry & Pattern
    FIRE_PARAM_ENUM("Shape", "Emitter", cfg->emitterShape, shapeNames, VFX_FIRE_EMITTER_COUNT);
    FIRE_PARAM_ENUM("Pattern", "Emitter", cfg->emissionMode, emitNames, VFX_FIRE_EMIT_COUNT);
    FIRE_PARAM_FLOAT("Source radius m", "Emitter", cfg->sourceRadius, 0.05f, 5.0f, 0.05f);
    FIRE_PARAM_FLOAT("Source length m", "Emitter", cfg->sourceLength, 0.2f, 10.0f, 0.2f);

    // Motion Field & Fluid Dynamics
    FIRE_PARAM_ENUM("Motion", "Motion", cfg->motionType, motionNames, VFX_FIRE_MOTION_COUNT);
    FIRE_PARAM_FLOAT("Field speed m/s", "Motion", cfg->fieldSpeed, 0.0f, 30.0f, 0.5f);
    FIRE_PARAM_FLOAT("Field radius m", "Motion", cfg->fieldRadius, 0.2f, 10.0f, 0.1f);
    FIRE_PARAM_FLOAT("Swirl m/s", "Motion", cfg->swirlSpeed, -30.0f, 30.0f, 0.5f);
    FIRE_PARAM_FLOAT("Turbulence m/s", "Motion", cfg->turbulenceSpeed, 0.0f, 20.0f, 0.5f);
    FIRE_PARAM_FLOAT("Buoyancy m/s2", "Motion", cfg->buoyancy, 0.0f, 25.0f, 0.5f);

    // Optics & Combustion Essence
    FIRE_PARAM_FLOAT("Intensity", "Optics", cfg->intensity, 0.5f, 5.0f, 0.1f);
    FIRE_PARAM_FLOAT("Core heat", "Optics", cfg->coreHeat, 0.5f, 3.0f, 0.05f);
    FIRE_PARAM_FLOAT("Distortion", "Optics", cfg->distortion, 0.0f, 1.0f, 0.01f);
    FIRE_PARAM_FLOAT("Softness", "Optics", cfg->softness, 0.05f, 1.0f, 0.01f);
    FIRE_PARAM_FLOAT("Emissive boost", "Optics", cfg->emissiveBoost, 0.5f, 4.0f, 0.05f);
    FIRE_PARAM_FLOAT("Flicker rate Hz", "Optics", cfg->flickerRate, 0.0f, 30.0f, 1.0f);
    FIRE_PARAM_BOOL("Emit sparks", "Optics", cfg->emitSparks);
    FIRE_PARAM_BOOL("Dynamic light", "Optics", cfg->dynamicLight);

#undef FIRE_PARAM_ENUM
#undef FIRE_PARAM_FLOAT
#undef FIRE_PARAM_INT
#undef FIRE_PARAM_BOOL

    return n;
}

static MotionFieldHandle s_unifiedFireFieldHandle = MOTION_FIELD_INVALID;
static float s_unifiedFireAccum = 0.0f;

void VFX_KillFire(int handle)
{
    (void)handle;
    if (s_unifiedFireFieldHandle != MOTION_FIELD_INVALID) {
        MotionFields_Stop(s_unifiedFireFieldHandle);
        s_unifiedFireFieldHandle = MOTION_FIELD_INVALID;
    }
}

static SkillCurve s_anchoredFollowCurve = {0};
static bool s_anchoredFollowInit = false;
static void FVol_EnsureAnchoredCurve(void) {
    if (!s_anchoredFollowInit) {
        s_anchoredFollowInit = true;
        FloatCurve_AddStop(&s_anchoredFollowCurve, 0.0f, 1.0f);
        FloatCurve_AddStop(&s_anchoredFollowCurve, 0.85f, 1.0f);
        FloatCurve_AddStop(&s_anchoredFollowCurve, 1.0f, 0.70f);
    }
}

int VFX_ComposeFireEx(Vector3 pos, const VFX_FireConfig *cfg)
{
    if (!cfg) cfg = &s_liveFireConfig;
    FVol_InitShared();
    FVol_EnsureProceduralNoise();
    FVol_EnsureAnchoredCurve();

    float dt = TimeFX_RawDelta();
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    static Vector3 s_anchoredEmitterPos = {0};
    s_anchoredEmitterPos = pos;

    // Detect preset changes on live config
    static VFX_FirePreset s_lastPreset = VFX_FIRE_PRESET_TORCH;
    if (cfg->preset != s_lastPreset && cfg->preset != VFX_FIRE_PRESET_CUSTOM) {
        s_lastPreset = cfg->preset;
        VFX_Fire_ApplyPreset((VFX_FireConfig*)cfg, cfg->preset);
    } else if (cfg->preset != s_lastPreset) {
        s_lastPreset = cfg->preset;
    }

    // 0. Sample dynamic environmental wind from Wind System
    Vector3 sceneWind = Wind_EvaluateVelocity(pos, TimeFX_Elapsed());
    float sceneWindSpeed = Vector3Length(sceneWind);

    // 1. Maintain or update Motion Field
    MotionFieldRecipe recipe = {0};
    recipe.origin = pos;
    recipe.axis = (Vector3){0.0f, 1.0f, 0.0f};
    recipe.durationSec = cfg->emitDuration > 0.0f ? (cfg->emitDuration + cfg->lifetime + 1.0f) : 3600.0f;
    recipe.attackSec = 0.05f;
    recipe.fadeSec = 0.3f;

    if (cfg->attachment == VFX_FIRE_ATTACH_ANCHORED) {
        // Contained local envelope around anchor
        recipe.radiusM = (cfg->sourceRadius > 0.05f ? cfg->sourceRadius * 2.2f : 0.8f);
        recipe.speedMps = fminf(cfg->fieldSpeed, 2.0f);
        recipe.swirlSpeedMps = fminf(cfg->swirlSpeed, 3.5f);
        recipe.turbulenceSpeedMps = fminf(cfg->turbulenceSpeed, 1.2f);
        recipe.gravityMps2 = -fminf(cfg->buoyancy, 4.5f);
        recipe.guidance = GUIDE_TIGHT;
        recipe.kind = MOTION_RECIPE_AIRFLOW;
    } else if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
        recipe.radiusM = cfg->fieldRadius > 0.1f ? cfg->fieldRadius : 2.0f;
        recipe.speedMps = fminf(cfg->fieldSpeed, 1.5f);
        recipe.swirlSpeedMps = 0.0f;
        recipe.turbulenceSpeedMps = fminf(cfg->turbulenceSpeed, 0.5f);
        recipe.gravityMps2 = 0.0f;
        recipe.guidance = GUIDE_BALANCED;
        recipe.kind = MOTION_RECIPE_AIRFLOW;
    } else {
        // Free ballistic
        recipe.radiusM = cfg->fieldRadius > 0.1f ? cfg->fieldRadius : 1.0f;
        recipe.speedMps = cfg->fieldSpeed;
        recipe.swirlSpeedMps = cfg->swirlSpeed;
        recipe.turbulenceSpeedMps = cfg->turbulenceSpeed;
        recipe.gravityMps2 = -cfg->buoyancy;
        recipe.guidance = GUIDE_BALANCED;
        if (cfg->motionType == VFX_FIRE_MOTION_BLAST) {
            recipe.kind = MOTION_RECIPE_BLAST;
            recipe.speedMps = (cfg->fieldSpeed > 1.0f) ? cfg->fieldSpeed : 6.0f;
        } else if (cfg->motionType == VFX_FIRE_MOTION_VORTEX) {
            recipe.kind = MOTION_RECIPE_AIRFLOW;
            recipe.swirlSpeedMps = cfg->swirlSpeed * 1.5f;
        } else if (cfg->motionType == VFX_FIRE_MOTION_JET) {
            recipe.kind = MOTION_RECIPE_AIRFLOW;
            Vector3 jetDir = (sceneWindSpeed > 0.05f)
                ? Vector3Normalize((Vector3){sceneWind.x, 0.2f * sceneWindSpeed, sceneWind.z})
                : (Vector3){0.0f, 0.2f, 1.0f};
            recipe.axis = jetDir;
            recipe.speedMps = (cfg->fieldSpeed > 1.0f) ? cfg->fieldSpeed : 12.0f;
        } else if (cfg->motionType == VFX_FIRE_MOTION_INWARD) {
            recipe.kind = MOTION_RECIPE_AIRFLOW;
            recipe.speedMps = -(cfg->fieldSpeed > 1.0f ? cfg->fieldSpeed : 4.0f);
        } else {
            recipe.kind = MOTION_RECIPE_AIRFLOW;
        }
    }

    static VFX_FireConfig s_prevAppliedCfg;
    static Vector3 s_prevAppliedPos = {0};
    static bool s_hasPrevCfg = false;

    bool recipeChanged = !s_hasPrevCfg ||
        s_prevAppliedCfg.motionType != cfg->motionType ||
        s_prevAppliedCfg.fieldSpeed != cfg->fieldSpeed ||
        s_prevAppliedCfg.fieldRadius != cfg->fieldRadius ||
        s_prevAppliedCfg.swirlSpeed != cfg->swirlSpeed ||
        s_prevAppliedCfg.turbulenceSpeed != cfg->turbulenceSpeed ||
        s_prevAppliedCfg.buoyancy != cfg->buoyancy ||
        s_prevAppliedCfg.attachment != cfg->attachment;

    bool posChanged = (Vector3Distance(s_prevAppliedPos, pos) > 0.01f);

    FieldDesc field;
    if (MotionFieldRecipe_Build(&recipe, &field)) {
        if (cfg->attachment != VFX_FIRE_ATTACH_ANCHORED && sceneWindSpeed > 0.01f) {
            field.flow.velocityMps = Vector3Add(field.flow.velocityMps, sceneWind);
        }
        if (recipeChanged || s_unifiedFireFieldHandle == MOTION_FIELD_INVALID || !MotionFields_IsAlive(s_unifiedFireFieldHandle)) {
            if (s_unifiedFireFieldHandle != MOTION_FIELD_INVALID && MotionFields_IsAlive(s_unifiedFireFieldHandle)) {
                MotionFields_Stop(s_unifiedFireFieldHandle);
            }
            s_unifiedFireFieldHandle = MotionFields_CreateField(&field);
            s_prevAppliedCfg = *cfg;
            s_prevAppliedPos = pos;
            s_hasPrevCfg = true;
        } else if (posChanged) {
            MotionFields_SetTransform(s_unifiedFireFieldHandle, &field.transform);
            s_prevAppliedPos = pos;
        }
    }

    // 2. Dynamic Lighting: Realistic warm flickering point light
    if (cfg->dynamicLight && cfg->intensity > 0.1f) {
        float flicker = 0.82f + 0.18f * sinf(TimeFX_Elapsed() * (cfg->flickerRate > 0.1f ? cfg->flickerRate : 14.0f));
        float lightRadius = (cfg->sourceRadius * 1.5f + cfg->fieldRadius * 0.8f + 1.2f) * flicker;
        float lightLife = dt * 1.5f;
        if (lightLife < 0.04f) lightLife = 0.04f;
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0.0f, cfg->sourceRadius * 0.5f + 0.25f, 0.0f}),
                       (Color){255, 145, 35, 255}, lightRadius, lightLife, VFX_PRIORITY_LOW);
    }

    // 3. Emission Rate Modulation
    float life = cfg->lifetime > 0.1f ? cfg->lifetime : 1.5f;
    int targetCount = cfg->particleCount > 0 ? cfg->particleCount : 50;
    if (targetCount > 1000) targetCount = 1000;

    float rate = (float)targetCount / life;
    if (cfg->emissionMode == VFX_FIRE_EMIT_PULSING) {
        float fHz = cfg->flickerRate > 0.1f ? cfg->flickerRate * 0.25f : 3.0f;
        rate *= (1.0f + 0.45f * sinf(TimeFX_Elapsed() * fHz * 2.0f * PI));
    } else if (cfg->emissionMode == VFX_FIRE_EMIT_SPRAY) {
        rate *= 1.35f;
    }

    s_unifiedFireAccum += dt * rate;
    int spawnNow = (int)s_unifiedFireAccum;
    s_unifiedFireAccum -= (float)spawnNow;
    if (spawnNow > 50) { spawnNow = 50; s_unifiedFireAccum = 0.0f; }
    if (spawnNow <= 0) return 0;

    // Direct particle radius from user configuration (meter-scale)
    float drawRadius = (cfg->particleRadius > 0.01f) ? cfg->particleRadius : 0.22f;

    // Resolve mode texture and volumeSheet
    Texture2D pTex = s_fvolProceduralNoiseTex;
    Texture2D pNormalTex = (Texture2D){0};
    int volSheet = 5; // Default Shader Tongue
    SpriteAnim *pAnim = NULL;
    float facingAspect = 1.0f;

    if (cfg->mode == VFX_FIRE_MODE_FLIPBOOK_TONGUE) {
        volSheet = 4;
        pTex = (s_fvolTongue01Tex.id != 0) ? s_fvolTongue01Tex : s_fvolRoilTex;
        pNormalTex = (s_fvolTongue01NormalTex.id != 0) ? s_fvolTongue01NormalTex : s_fvolRoilNormalTex;
        pAnim = (s_fvolTongue01Anim.frameCount > 0) ? &s_fvolTongue01Anim : &s_fvolRoilAnim;
        facingAspect = 1.35f;
    } else if (cfg->mode == VFX_FIRE_MODE_FLIPBOOK_PUFF) {
        volSheet = 4;
        pTex = (s_fvolRoilTex.id != 0) ? s_fvolRoilTex : s_fvolPuffTex;
        pNormalTex = s_fvolRoilNormalTex;
        pAnim = (s_fvolRoilAnim.frameCount > 0) ? &s_fvolRoilAnim : &s_fvolPuffAnim;
        facingAspect = 1.0f;
    } else if (cfg->mode == VFX_FIRE_MODE_SHADER_TONGUE) {
        volSheet = 5;
        pTex = s_fvolProceduralNoiseTex;
        pAnim = NULL;
        facingAspect = 1.75f;
    } else { // VFX_FIRE_MODE_SHADER_PUFF
        volSheet = 6;
        pTex = s_fvolProceduralNoiseTex;
        pAnim = NULL;
        facingAspect = 1.0f;
    }

    static const ParticleDynamicsProfile s_anchoredDynamics = {
        .inverseMassKg = 1.0f,
        .gravityScale = 0.0f,
        .linearDragPerSecond = 5.5f,
        .terminalSpeedMps = 1.6f,
        .windCouplingHz = 0.5f,
        .windSusceptibility = 0.05f
    };
    static const ParticleDynamicsProfile s_surfaceClingDynamics = {
        .inverseMassKg = 1.0f,
        .gravityScale = 0.0f,
        .linearDragPerSecond = 4.2f,
        .terminalSpeedMps = 1.2f,
        .windCouplingHz = 0.6f,
        .windSusceptibility = 0.12f
    };
    static const ParticleDynamicsProfile s_freeDynamics = {
        .inverseMassKg = 5.0f,
        .gravityScale = 0.0f,
        .linearDragPerSecond = 0.4f,
        .terminalSpeedMps = 0.0f,
        .windCouplingHz = 4.5f,
        .windSusceptibility = 1.0f
    };

    float effectiveLife = life;
    if (cfg->attachment == VFX_FIRE_ATTACH_ANCHORED) {
        // Confine the flame envelope proportionally to both source footprint and particle scale
        float flameDimension = fmaxf(cfg->sourceRadius, drawRadius * 1.2f);
        float maxFlameHeight = (flameDimension * 1.8f + 0.35f) * (cfg->riseMultiplier > 0.1f ? cfg->riseMultiplier : 1.0f);
        effectiveLife = fminf(life, maxFlameHeight / 0.85f);
        if (effectiveLife < 0.25f) effectiveLife = 0.25f;
    } else if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
        effectiveLife = fminf(life, 1.2f);
    }

    static unsigned int s_seedCounter = 0;
    for (int i = 0; i < spawnNow; ++i) {
        s_seedCounter++;
        float phase = fmodf((float)s_seedCounter * 0.6180339f, 1.0f);
        float ang = phase * 2.0f * PI;

        // 4. Calculate spawn position based on Emitter Shape
        Vector3 p = pos;
        switch (cfg->emitterShape) {
        case VFX_FIRE_EMITTER_POINT:
            p.x += (fmodf(phase * 11.3f, 1.0f) - 0.5f) * 0.04f;
            p.y += (fmodf(phase * 7.1f, 1.0f) - 0.5f) * 0.04f;
            p.z += (fmodf(phase * 13.9f, 1.0f) - 0.5f) * 0.04f;
            break;
        case VFX_FIRE_EMITTER_CIRCLE: {
            float r = sqrtf(fmodf(phase * 7.13f, 1.0f)) * cfg->sourceRadius;
            p.x += cosf(ang) * r;
            p.z += sinf(ang) * r;
            break;
        }
        case VFX_FIRE_EMITTER_SPHERE: {
            float u = fmodf(phase * 5.17f, 1.0f) * 2.0f - 1.0f;
            float rSph = cbrtf(fmodf(phase * 9.31f, 1.0f)) * cfg->sourceRadius;
            float rCross = sqrtf(fmaxf(0.0f, 1.0f - u * u));
            p.x += rCross * cosf(ang) * rSph;
            p.y += u * rSph;
            p.z += rCross * sinf(ang) * rSph;
            break;
        }
        case VFX_FIRE_EMITTER_LINE: {
            float t = fmodf(phase * 17.1f, 1.0f) - 0.5f;
            Vector3 lineDir = (sceneWindSpeed > 0.05f)
                ? Vector3Normalize((Vector3){-sceneWind.z, 0.0f, sceneWind.x})
                : (Vector3){1.0f, 0.0f, 0.0f};
            p = Vector3Add(p, Vector3Scale(lineDir, t * (cfg->sourceLength > 0.1f ? cfg->sourceLength : 2.0f)));
            break;
        }
        case VFX_FIRE_EMITTER_RING: {
            p.x += cosf(ang) * cfg->sourceRadius;
            p.z += sinf(ang) * cfg->sourceRadius;
            break;
        }
        case VFX_FIRE_EMITTER_HEMISPHERE: {
            float u = fmodf(phase * 5.17f, 1.0f); // upper hemisphere
            float rHemi = cbrtf(fmodf(phase * 9.31f, 1.0f)) * cfg->sourceRadius;
            float rCross = sqrtf(fmaxf(0.0f, 1.0f - u * u));
            p.x += rCross * cosf(ang) * rHemi;
            p.y += u * rHemi;
            p.z += rCross * sinf(ang) * rHemi;
            break;
        }
        default:
            break;
        }

        // Surface Cling clamping
        if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
            p.y = MapManager_GetGroundHeightAt(p.x, p.z) + 0.02f;
        }

        // 5. Calculate initial velocity based on Motion Field & Pattern
        Vector3 vel;
        if (cfg->attachment == VFX_FIRE_ATTACH_ANCHORED) {
            // Rooted flame: gentle vertical lick + inward centripetal convergence toward axis + ambient wind tilt
            float riseSpd = (0.32f + fmodf(phase * 1.7f, 0.28f)) * (cfg->riseMultiplier > 0.1f ? cfg->riseMultiplier : 1.0f);
            Vector3 toAxis = Vector3Subtract(pos, p);
            toAxis.y = 0.0f;
            float distFromAxis = Vector3Length(toAxis);
            Vector3 inwardVel = {0};
            if (distFromAxis > 0.001f) {
                inwardVel = Vector3Scale(Vector3Normalize(toAxis), distFromAxis * 0.95f);
            }
            vel = (Vector3){
                inwardVel.x + sceneWind.x * 0.08f,
                riseSpd,
                inwardVel.z + sceneWind.z * 0.08f
            };
            if (cfg->motionType == VFX_FIRE_MOTION_VORTEX) {
                Vector3 rVec = Vector3Subtract(p, pos);
                rVec.y = 0.0f;
                Vector3 tangent = {-rVec.z, 0.0f, rVec.x};
                if (Vector3Length(tangent) > 0.001f) {
                    tangent = Vector3Normalize(tangent);
                    vel = Vector3Add(vel, Vector3Scale(tangent, cfg->swirlSpeed * 0.18f));
                }
            }
        } else if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
            float creepSpd = (0.15f + fmodf(phase * 1.3f, 0.25f));
            vel = (Vector3){
                cosf(ang) * creepSpd,
                0.04f,
                sinf(ang) * creepSpd
            };
        } else {
            vel = (Vector3){
                cosf(ang) * 0.08f,
                (0.8f + fmodf(phase * 3.9f, 0.6f)) * cfg->riseMultiplier,
                sinf(ang) * 0.08f
            };
            if (cfg->motionType == VFX_FIRE_MOTION_BLAST) {
                Vector3 outDir = Vector3Normalize(Vector3Subtract(p, pos));
                if (Vector3Length(outDir) < 0.01f) outDir = (Vector3){cosf(ang), 0.6f, sinf(ang)};
                float spd = (cfg->fieldSpeed > 1.0f ? cfg->fieldSpeed : 5.0f) * (0.8f + 0.4f * fmodf(phase * 3.3f, 1.0f));
                vel = Vector3Scale(outDir, spd);
            } else if (cfg->motionType == VFX_FIRE_MOTION_JET) {
                float spd = (cfg->fieldSpeed > 1.0f ? cfg->fieldSpeed : 12.0f);
                Vector3 fwd = (sceneWindSpeed > 0.05f)
                    ? Vector3Normalize((Vector3){sceneWind.x, 0.12f * sceneWindSpeed, sceneWind.z})
                    : (Vector3){0.0f, 0.12f, 1.0f};
                Vector3 right = {-fwd.z, 0.0f, fwd.x};
                float rx = (fmodf(phase * 11.3f, 1.0f) - 0.5f) * 0.30f;
                float ry = (fmodf(phase * 7.7f, 1.0f) - 0.5f) * 0.25f;
                vel = Vector3Scale(Vector3Normalize(Vector3Add(fwd, Vector3Add(Vector3Scale(right, rx), (Vector3){0, ry, 0}))), spd);
            } else if (cfg->motionType == VFX_FIRE_MOTION_VORTEX) {
                Vector3 rVec = Vector3Subtract(p, pos);
                rVec.y = 0.0f;
                Vector3 tangent = {-rVec.z, 0.0f, rVec.x};
                if (Vector3Length(tangent) > 0.001f) {
                    tangent = Vector3Normalize(tangent);
                    vel = Vector3Add(vel, Vector3Scale(tangent, cfg->swirlSpeed * 0.45f));
                }
            }
        }

        ParticleConfig pc = {0};
        pc.position = p;
        pc.velocity = vel;
        pc.radius = drawRadius * (0.85f + 0.35f * fmodf(phase * 17.3f, 1.0f));
        pc.lifetime = effectiveLife * (0.80f + 0.40f * fmodf(phase * 29.7f, 1.0f));
        pc.colorStart = (Color){(unsigned char)fminf(255.0f, cfg->intensity * 60.0f), 0, (unsigned char)(phase * 255.0f), 255};
        pc.colorEnd = (Color){(unsigned char)fminf(255.0f, cfg->intensity * 20.0f), 0, (unsigned char)(phase * 255.0f), 0};
        pc.alphaCurve = &s_fvolFade;
        pc.speedCurve = &s_fvolRise;
        pc.emissiveCurve = &s_fvolCool;
        pc.radiusCurve = &s_fvolGrow;
        pc.facingAspect = facingAspect;

        if (cfg->attachment == VFX_FIRE_ATTACH_ANCHORED) {
            pc.followTarget = &s_anchoredEmitterPos;
            pc.followStrength = 1.0f;
            pc.followCurve = &s_anchoredFollowCurve;
            pc.windInfluence = 0.04f;
            pc.drag = 5.0f;
            pc.physics.dynamics = &s_anchoredDynamics;
        } else if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
            pc.followTarget = &s_anchoredEmitterPos;
            pc.followStrength = 0.85f;
            pc.windInfluence = 0.15f;
            pc.drag = 3.5f;
            pc.physics.dynamics = &s_surfaceClingDynamics;
        } else {
            pc.followTarget = NULL;
            pc.followStrength = 0.0f;
            pc.windInfluence = 1.0f;
            pc.drag = 0.2f;
            pc.physics.dynamics = &s_freeDynamics;
        }

        pc.physics.receiveMotionFields = true;
        pc.physics.spatialMotionOnly = true;

        pc.render.volumeSheet = volSheet;
        pc.render.texture = pTex;
        pc.render.normalTex = pNormalTex;
        pc.render.rampLUT = FVol_RampLUT(VC_MAT_FIRE);
        pc.render.heatGain = cfg->coreHeat * cfg->riseMultiplier;
        pc.render.smokeGain = cfg->softness;
        pc.render.emissiveBoost = cfg->intensity * cfg->emissiveBoost;
        pc.render.motionWarpScale = cfg->distortion;
        pc.render.blendMode = VFX_BLEND_PREMULTIPLIED;

        if (pAnim != NULL && pAnim->frameCount > 0) {
            pc.spriteAnim = pAnim;
            pc.spriteAnimPhase = phase * FVOL_BODY_PHASE_MAX;
            pc.spriteAnimRate = 0.85f + 0.30f * fmodf(phase * 11.1f, 1.0f);
        }

        SpawnParticle(pc);

        // 6. Secondary Effect: Shed Sparks / Embers fluttering from flame tips
        if (cfg->emitSparks && (s_seedCounter % 6 == 0)) {
            ParticleConfig sc = {0};
            sc.position = Vector3Add(p, (Vector3){0.0f, drawRadius * 0.8f, 0.0f});
            if (cfg->attachment == VFX_FIRE_ATTACH_ANCHORED) {
                float sparkSpd = 0.38f + fmodf(phase * 3.7f, 0.32f);
                sc.velocity = (Vector3){cosf(ang) * 0.06f, sparkSpd * (cfg->riseMultiplier > 0.1f ? cfg->riseMultiplier : 1.0f), sinf(ang) * 0.06f};
                sc.lifetime = effectiveLife * 0.60f;
                sc.followTarget = &s_anchoredEmitterPos;
                sc.followStrength = 0.95f;
                sc.windInfluence = 0.06f;
                sc.drag = 4.0f;
                sc.physics.dynamics = &s_anchoredDynamics;
            } else if (cfg->attachment == VFX_FIRE_ATTACH_SURFACE_CLING) {
                sc.velocity = (Vector3){cosf(ang) * 0.20f, 0.08f, sinf(ang) * 0.20f};
                sc.lifetime = 0.40f;
                sc.windInfluence = 0.20f;
                sc.drag = 2.5f;
                sc.physics.dynamics = &s_surfaceClingDynamics;
            } else {
                float sparkSpd = 1.4f + fmodf(phase * 7.7f, 1.8f);
                sc.velocity = (Vector3){cosf(ang) * 0.4f, sparkSpd * cfg->riseMultiplier, sinf(ang) * 0.4f};
                sc.lifetime = life * 0.55f;
                sc.windInfluence = 1.0f;
                sc.physics.dynamics = &s_freeDynamics;
            }
            sc.radius = drawRadius * 0.12f;
            sc.colorStart = (Color){255, 240, 180, 255};
            sc.colorEnd = (Color){255, 110, 20, 0};
            sc.physics.receiveMotionFields = true;
            sc.physics.spatialMotionOnly = true;
            sc.render.blendMode = VFX_BLEND_ADDITIVE;
            SpawnParticle(sc);
        }
    }

    return spawnNow;
}
