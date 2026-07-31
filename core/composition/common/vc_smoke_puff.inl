// vc_smoke_puff.inl — Đợt E / F2, the FIRST VFX rebuilt on the F1 foundation.
//
// Why a puff and not something showier (ELDEN_VFX_SPEC.md §F2 "START HERE"):
//   1. It is the smallest thing that exercises all four root causes at once —
//      lighting, alpha over additive, layered sprites, silhouette. If F1 is
//      wrong, this reveals it before anything is built on top.
//   2. Almost everything else consumes it: impact dust, footsteps, landings,
//      the base of an explosion, the smoke fire cools into (F3), ground scuffs.
//      A smoke COLUMN and an explosion are this puff with a different spawn
//      pattern and wind — get the unit right and the rest is orchestration.
//   3. It is objectively judgeable. "Is this aura good" cannot validate a
//      foundation; "does this look like dust" can.
//
// The old VFX_ComposeSmokePuff (vc_neutral.inl, deleted in the 2026-07 purge)
// was a handful of additive sprites at constant size. Every difference below is
// deliberate:
//
//   - ALPHA, never additive. Additive output can never be darker than its
//     background, which is why the old one read as glowing gas. Smoke occludes.
//   - MANY sprites with per-particle rotation. Identical un-rotated sprites read
//     as repeated stamps instantly, and one quad has no internal parallax.
//   - GROWS while it FADES (radiusCurve up, alphaCurve down). Smoke expands and
//     thins; constant-size puffs read as a decal popping in and out.
//   - DARK body colour. Brightness comes from the light (F1), not from the
//     texture. Authoring bright smoke defeats the entire lighting pass.
//
// Requires particle lighting to be ON to look right:
//   tuning.cfg → particle_lighting_strength = 1.0, particle_scatter_strength = 0.8
// At strength 0 this still works, it just reads flat — which is exactly the A/B
// that proves F1 is doing something.

#include "core/tuning.h"
#include "core/resource_manager.h"

#define SMOKE_PUFF_MAX_SPRITES 28

static SkillCurve s_smokePuffGrow  = {0};
// A SECOND, much flatter growth curve used only with the flipbook.
//
// The aggressive 0.45->2.2x curve above exists because a FLAT sprite does not
// expand on its own — "smoke expands and thins; constant-size puffs read as a
// decal popping in and out". The flipbook already expands: measured, the sheet
// grows 1.46x by itself. Multiplying the two gives 7.2x apparent growth, so the
// sprite balloons while its content is also ballooning.
//
// Worse, the sheet's own width WOBBLES frame to frame (measured +-7%: 162, 153,
// 173, 155, 150, 167 px...). Riding that wobble on top of a steep scale ramp is
// what reads as pulsing rather than drifting — the "vừa lớn lên, vừa đổi khung
// hình" the owner spotted. Flattening the curve lets the sheet own the growth.
static SkillCurve s_smokePuffGrowFb = {0};
static SkillCurve s_smokePuffFade  = {0};
static ColorGradient s_smokePuffGrad = {0};
static ForceField s_smokePuffFld = {0};
static bool s_smokePuffInit = false;

// Live-tunable so the F1 verification can separate two very different failures:
// "the lighting is broken" versus "the lighting works but 28 overlapping soft
// sprites average it away". Drop the count to 2-3 and the shading on a single
// sprite is unmistakable; raise it back and watch it dissolve into mush. That
// dissolve is not a bug in F1 — it is why F2 needs authored silhouettes (E4)
// rather than more of the same soft radial blob.
static float s_smokePuffCountMul = 1.4f;
static float s_smokePuffSizeMul  = 1.0f;
// Per-sprite opacity. LOW on purpose. Once the sprites gained a real silhouette
// they stopped being a soft smear and started reading as separate stamps, because
// each one was nearly opaque and they all had similar radii. Smoke is built from
// many FAINT layers whose overlaps accumulate — an individual sprite should be
// barely visible on its own. Raise this and the puff turns back into clumps.
static float s_smokePuffAlpha    = 0.28f;
// Spread of sprite radii. Uniform sizes read as repetition no matter how the
// silhouettes differ; a wide spread is what makes overlaps look like structure.
static float s_smokePuffSizeVar  = 1.4f;

// Three lobed-silhouette variants (scripts/generate_smoke_sprite.py). The stock
// particle texture is a plain radial gradient, which has no OUTLINE — lighting a
// featureless blob yields a lit featureless blob, and 28 of them average back to
// a uniform smear. That is the ceiling F1's shading kept running into once the
// maths was proven correct (ELDEN_VFX_SPEC.md §0.1b cause 3).
//
// Three, not one: a single sprite repeated 28 times reads as stamps no matter
// how much each is rotated.
#define SMOKE_PUFF_VARIANTS 3
static Texture2D s_smokePuffTex[SMOKE_PUFF_VARIANTS];

// ── Đợt E / E4 — the authored flipbook ──────────────────────────────────────
// The three static sprites above give a SILHOUETTE but no MOTION: every sprite
// is frozen, so a puff is a cloud of stamps that only moves because the
// particles translate. Real smoke's billow rolls — the outline itself changes
// shape — and no amount of translating a fixed cutout produces that.
// `smoke_atlas_8x8.png` is a 64-frame simulated puff (birth → billow →
// dissipate) and the particle system already knows how to play an atlas
// (ParticleConfig.spriteAnim); nothing consumed it until now.
//
// The static sprites REMAIN as the fallback: the spec requires every consumer
// to keep a procedural/asset-free path so a build never depends on an asset
// landing, and this one costs nothing to keep.
static Texture2D s_smokeFbTex = {0};
// FOUR templates, not one, at slightly different playback rates.
//
// SpriteAnim_CalculateUV derives the frame from the particle's ABSOLUTE age
// (frame = age * fps) and there is no per-particle phase offset in the API. Every
// sprite in a puff spawns on the SAME frame, so with a single template they all
// carry age 0 together and then step to each new atlas frame IN LOCKSTEP — ~20
// sprites all snapping to a different billow shape at the same instant, several
// times a second. That synchronized snap is what reads as churning; the
// per-sprite spin was only part of it.
//
// Giving each sprite a slightly different rate desynchronises them within a few
// frames, so the puff dissolves between shapes instead of cutting between them.
// Rates are jittered DOWNWARD only: a faster template would reach the end of the
// 64-frame sheet before a long-lived particle dies and then hold frame 63, which
// is EMPTY — the smoke would disappear while its alpha says it is still there.
#define SMOKE_FB_RATES 4
static SpriteAnim s_smokeFbAnim[SMOKE_FB_RATES];
static bool       s_smokeFbReady = false;
// 1 = flipbook when available, 0 = force the old static sprites (A/B by eye,
// no rebuild — the flipbook changes F2's look and that is a judgement call).
static float s_smokePuffUseFlipbook = 1.0f;
// Flipbook-only corrections to the static-sprite tuning. Every knob F2 was tuned
// with — many sprites, per-sprite spin, wide size spread — exists to fake INTERNAL
// MOTION that flat cutouts do not have. The flipbook has real internal motion, so
// those same compensations stop helping and start fighting it: 39 cutouts each
// spinning while each also plays its own 64-frame billow reads as churning, not
// as smoke. Reported as "từng khung hình thì giống khói, nhưng chuyển động giữa
// các khung thì hỗn loạn, quay cuồng" — which is exactly this.
static float s_smokePuffFbSpin     = 0.12f; // x on angular velocity (1.0 = the old spin)
static float s_smokePuffFbCountMul = 0.55f; // x on sprite count
// Size compensation. Dropping the 0.45->2.2x curve for the flat one removes most
// of the apparent growth, so without this the flipbook puff would simply be
// SMALLER than the tuned static one — and this change is meant to be about
// smoothness, not size. 1.45 ~ the ratio of the two curves' mean scale.
static float s_smokePuffFbSizeMul  = 1.45f;
// How many of the sheet's 64 frames to actually play.
//
// The tail of the sim is where the puff breaks into thin wisps, and those wisps
// genuinely change shape frame to frame — measured, it is the SHEET's content,
// not the consumer: turning the cross-fade off barely moves the rim's
// frame-to-frame change (16.4 vs 16.9), and the same is true of the texture
// filter. Since the sprite is also at its LARGEST then, that rim churn is
// magnified most exactly when it is least wanted.
//
// Stopping short lets ANIM_ONCE hold a calmer mid-dissipation frame while the
// alpha curve finishes the fade — the puff still thins out and vanishes, it just
// stops crawling while it does. 64 = play the whole sheet.
static float s_smokePuffFbFrames   = 50.0f;

static void SmokePuff_InitShared(void)
{
    if (s_smokePuffInit)
        return;

    // Registered lazily (first spawn), never from an Init: Tuning_RegisterFloat
    // only reads the config once Tuning_Init has set the path — see
    // core/docs/LANDMINES.md.
    Tuning_RegisterFloat("smokepuff_count_mul", &s_smokePuffCountMul, 1.4f);
    Tuning_RegisterFloat("smokepuff_size_mul", &s_smokePuffSizeMul, 1.0f);
    Tuning_RegisterFloat("smokepuff_alpha", &s_smokePuffAlpha, 0.28f);
    Tuning_RegisterFloat("smokepuff_size_var", &s_smokePuffSizeVar, 1.4f);
    Tuning_RegisterFloat("smokepuff_flipbook", &s_smokePuffUseFlipbook, 1.0f);
    Tuning_RegisterFloat("smokepuff_fb_spin", &s_smokePuffFbSpin, 0.12f);
    Tuning_RegisterFloat("smokepuff_fb_count", &s_smokePuffFbCountMul, 0.55f);
    Tuning_RegisterFloat("smokepuff_fb_size", &s_smokePuffFbSizeMul, 1.45f);
    Tuning_RegisterFloat("smokepuff_fb_frames", &s_smokePuffFbFrames, 50.0f);

    // Grows to ~2.2x over its life and never shrinks back — smoke does not
    // contract, it dissipates. The fade curve is what removes it.
    FloatCurve_AddStop(&s_smokePuffGrow, 0.0f, 0.45f);
    FloatCurve_AddStop(&s_smokePuffGrow, 0.25f, 1.0f);
    FloatCurve_AddStop(&s_smokePuffGrow, 1.0f, 2.2f);

    // Flipbook: a gentle drift only. Not 1.0 flat — a little scale motion still
    // helps the sprite read as receding, and it hides the sheet's own wobble
    // rather than amplifying it.
    FloatCurve_AddStop(&s_smokePuffGrowFb, 0.0f, 0.90f);
    FloatCurve_AddStop(&s_smokePuffGrowFb, 0.30f, 1.05f);
    FloatCurve_AddStop(&s_smokePuffGrowFb, 1.0f, 1.30f);

    // Fast in, slow out: a puff appears almost instantly then lingers. Front-
    // loading the fade instead makes it read as a muzzle flash.
    FloatCurve_AddStop(&s_smokePuffFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_smokePuffFade, 0.12f, 1.0f);
    FloatCurve_AddStop(&s_smokePuffFade, 0.55f, 0.75f);
    FloatCurve_AddStop(&s_smokePuffFade, 1.0f, 0.0f);

    // Deliberately dark and desaturated. These are near-black greys; the lit
    // shader lifts them. If this gradient looks "too dark" in the file, that is
    // the point — see the header comment.
    ColorGradient_AddStop(&s_smokePuffGrad, 0.0f, (Color){96, 92, 88, 255});
    ColorGradient_AddStop(&s_smokePuffGrad, 0.35f, (Color){74, 71, 68, 255});
    ColorGradient_AddStop(&s_smokePuffGrad, 1.0f, (Color){44, 42, 40, 255});

    // Gentle buoyancy + drag. Meter-scale: real gravity is 9.81, so 0.35 up is
    // a slow drift, not a launch.
    ForceField_AddLayer(&s_smokePuffFld, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = 0.35f,
    });
    ForceField_AddLayer(&s_smokePuffFld, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 1.6f,
    });

    static const char *paths[SMOKE_PUFF_VARIANTS] = {
        "assets/textures/smoke_puff_01.png",
        "assets/textures/smoke_puff_02.png",
        "assets/textures/smoke_puff_03.png",
    };
    for (int i = 0; i < SMOKE_PUFF_VARIANTS; i++)
    {
        s_smokePuffTex[i] = ResourceManager_LoadTexture(paths[i]);
        if (s_smokePuffTex[i].id == 0)
            TraceLog(LOG_WARNING,
                     "SMOKE PUFF: %s missing — falling back to the default particle "
                     "texture (regenerate: python3 scripts/generate_smoke_sprite.py)",
                     paths[i]);
    }

    // E4 flipbook. 8x8 = 64 frames covering one full puff life.
    //
    // fps is derived, not picked: SpriteAnim_CalculateUV advances on ABSOLUTE
    // age (frame = age * fps) while these particles live 1.1-2.0 s. Setting fps
    // from the LONGEST lifetime means the oldest particle only just reaches the
    // final frame; picking the average instead would let long-lived sprites run
    // off the end early and then hold frame 63 — which is empty — so the smoke
    // would VANISH while its alpha curve says it should still be visible.
    // ANIM_ONCE clamps, so shorter-lived particles simply end mid-dissipation,
    // which their own alpha fade hides.
    // TWO sheets with DIFFERENT CONTRACTS, and the difference is not cosmetic.
    //
    //   smoke_puff_8x8_smoke.png (preferred) — from scripts/flipbook/. RGB is
    //     the fraction of light that survives to each pixel (the volume's OWN
    //     shadow, marched at bake time), alpha is the density mask. Measured
    //     internal value spread p10..p90 = 0.69.
    //   smoke_atlas_8x8.png (fallback) — same contract, older sheet. Owner
    //     rejected it: R and G identical at 21.0% (one greyscale channel
    //     tripled, from before the channel layout existed), silhouette shredded
    //     rather than billowed (lobes 2.31 vs 1.08), framed 1.31x small, and
    //     value spread only 0.31.
    //
    // BOTH carry their own value, which is why the vertex colour is lifted for
    // either (see the spawn site). A flat-white MASK cannot be substituted here:
    // the lighting pass shades a BILLBOARD and knows nothing about the depth
    // inside the puff, so an unshaded sheet stacks into flat cards — measured at
    // value spread 0.00, which is what "những mảng màu riêng biệt" was.
    s_smokeFbTex = ResourceManager_LoadTexture("assets/textures/smoke_puff_8x8_smoke.png");
    if (s_smokeFbTex.id == 0)
        s_smokeFbTex = ResourceManager_LoadTexture("assets/textures/smoke_atlas_8x8.png");
    // WHICH sheet loaded, said out loud. Until now a silent fallback from the
    // new sheet to the old one — or from either to the three static sprites —
    // looked identical on screen to a working flipbook, and "the sprites hold
    // one frame" had no way to be told apart from "the sprites animate".
    TraceLog(s_smokeFbTex.id != 0 ? LOG_INFO : LOG_WARNING,
             "SMOKE PUFF: flipbook %s (tex id %u)",
             s_smokeFbTex.id != 0 ? "loaded" : "MISSING",
             (unsigned)s_smokeFbTex.id);

    if (s_smokeFbTex.id != 0)
    {
        // BILINEAR. raylib's default is POINT, and at the end of a puff's life
        // the sprite is at its largest, so one 256px atlas cell is magnified
        // across a big chunk of screen — with point sampling every texel becomes
        // a hard block. The sheet's rim carries real render noise (measured ~8-10
        // of 255 high-frequency energy in the soft edge band, a Cycles volume at
        // low sample counts), and magnifying that noise as blocks is what makes
        // the outline appear to wriggle as the frames advance.
        //
        // Bilinear only, NOT mipmaps: at coarse mip levels an 8x8 atlas bleeds
        // neighbouring cells into each other. Bilinear's own one-texel bleed is
        // harmless here because the puff covers ~20% of its cell, centred, so the
        // rim never reaches the cell boundary.
        SetTextureFilter(s_smokeFbTex, TEXTURE_FILTER_BILINEAR);
        static const float rateMul[SMOKE_FB_RATES] = { 1.0f, 0.90f, 0.81f, 0.72f };
        int fbFrames = (int)s_smokePuffFbFrames;
        if (fbFrames < 8)  fbFrames = 8;
        if (fbFrames > 64) fbFrames = 64;
        // fps stays derived from the frames actually played over the longest
        // lifetime, so trimming the tail slows playback rather than ending early.
        for (int r = 0; r < SMOKE_FB_RATES; r++)
            SpriteAnim_Init(&s_smokeFbAnim[r], 8, 8, fbFrames,
                            ((float)fbFrames / 2.0f) * rateMul[r], ANIM_ONCE);
        s_smokeFbReady = true;
    }
    else
    {
        // Announce the fallback: a silently-static puff and a flipbook that
        // failed to load look identical in a screenshot.
        TraceLog(LOG_WARNING,
                 "SMOKE PUFF: neither smoke_puff_8x8_smoke.png nor smoke_atlas_8x8.png "
                 "loaded — falling back to the 3 static sprites (no billow animation).");
    }

    s_smokePuffInit = true;
}

// One dust/smoke puff at `pos`. `scale` 1.0 ≈ a 1 m ground impact.
// `density` 0..1 scales sprite count (1.0 = SMOKE_PUFF_MAX_SPRITES).
//
// Draw it with BLEND_ALPHA. Adding an additive glow on top is fine and is how
// glowing smoke is done — as a SECOND draw, never by flipping this one to
// additive (ELDEN_VFX_SPEC.md F1b, the blend law).
void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density)
{
    SmokePuff_InitShared();

    const VFX_ElementMaterial *mat = VFX_Material(matId);

    if (density <= 0.0f) density = 1.0f;
    else if (density > 1.0f) density = 1.0f;
    bool useFb = s_smokeFbReady && (s_smokePuffUseFlipbook > 0.5f);
    // Each flipbook sprite is a whole puff simulation, not a stamp — stacking as
    // many as the flat version needs averages them into mush AND multiplies the
    // churn. Fewer, larger, calmer sprites read as more smoke, not less.
    int count = (int)(SMOKE_PUFF_MAX_SPRITES * density * s_smokePuffCountMul
                      * (useFb ? s_smokePuffFbCountMul : 1.0f));
    if (count < 1) count = 1;
    scale *= s_smokePuffSizeMul;

    // Neutral dust by default; an element tints it. Kept subtle on purpose —
    // heavily tinted smoke stops reading as smoke.
    bool neutral = (matId == VC_MAT_EARTH || matId == VC_MAT_COUNT);

    for (int i = 0; i < count; i++)
    {
        // Spawn across a small disc rather than a point, so the puff has a
        // silhouette from frame one instead of expanding out of a dot.
        float ang = Random01() * 2.0f * PI;
        float rad = sqrtf(Random01()) * 0.28f * scale;   // sqrt = even area spread
        Vector3 p = {
            pos.x + cosf(ang) * rad,
            pos.y + Random01() * 0.12f * scale,
            pos.z + sinf(ang) * rad,
        };

        // Outward-and-up, faster at the rim — the roll a real puff has.
        float outward = Math_Mix(0.35f, 1.1f, Random01()) * scale;
        Vector3 vel = {
            cosf(ang) * outward,
            Math_Mix(0.25f, 0.7f, Random01()) * scale,
            sinf(ang) * outward,
        };

        Color c = ColorGradient_Sample(&s_smokePuffGrad, Random01());
        if (useFb)
        {
            // THE FLIPBOOK ALREADY CARRIES ITS OWN VALUE. The atlas is a shaded
            // render (mean RGB ~121/255 inside the puff), while this gradient is
            // deliberately near-black because the static sprites are flat masks
            // that the lighting pass is supposed to lift. Multiplying the two
            // lands at ~33/255 — measured — and the puff reads as a black smudge.
            // With the flipbook the sprite supplies the value, so the vertex
            // colour must step back and only TINT. This is what the spec's name
            // `fb_smoke_lit_*` means: the sheet is already lit.
            c.r = (unsigned char)(160 + (c.r >> 2));
            c.g = (unsigned char)(160 + (c.g >> 2));
            c.b = (unsigned char)(160 + (c.b >> 2));
        }
        if (!neutral)
        {
            // Pull a third of the way toward the element body colour. Any more
            // and it reads as coloured gas rather than lit smoke.
            c.r = (unsigned char)((c.r * 2 + mat->body.r) / 3);
            c.g = (unsigned char)((c.g * 2 + mat->body.g) / 3);
            c.b = (unsigned char)((c.b * 2 + mat->body.b) / 3);
        }

        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = vel,
            // Wide, non-uniform radii: a few big soft ones read as the body,
            // many small ones as the broken edge.
            .radius = Math_Mix(0.14f, 0.14f + 0.42f * s_smokePuffSizeVar,
                               powf(Random01(), 1.8f)) * scale
                      * (useFb ? s_smokePuffFbSizeMul : 1.0f),
            .lifetime = Math_Mix(1.1f, 2.0f, Random01()),
            .colorStart = VC_WithAlpha(c, (unsigned char)(255.0f *
                              (s_smokePuffAlpha < 0.0f ? 0.0f :
                               s_smokePuffAlpha > 1.0f ? 1.0f : s_smokePuffAlpha))),
            .colorEnd = VC_WithAlpha(c, 0),
            .forceField = &s_smokePuffFld,
            .radiusCurve = (useFb ? &s_smokePuffGrowFb : &s_smokePuffGrow),
            .alphaCurve = &s_smokePuffFade,
            // Per-sprite spin. Without this the repeated texture is obvious no
            // matter how many sprites are stacked.
            // Flipbook when it loaded, static silhouettes otherwise. Variety
            // across sprites comes from their different spawn times and
            // lifetimes (so they are on different frames) plus the per-sprite
            // spin below — SpriteAnim has no per-particle frame offset.
            .render.texture = (useFb ? s_smokeFbTex : s_smokePuffTex[i % SMOKE_PUFF_VARIANTS]),
            .spriteAnim = (useFb ? &s_smokeFbAnim[i % SMOKE_FB_RATES] : NULL),
            .rotation = Random01() * 2.0f * PI,
            // Spin is all but OFF for the flipbook (see s_smokePuffFbSpin): the
            // sheet supplies its own motion, and rigid-body rotation on top of a
            // billow that is already rolling is what reads as churning. A trace
            // is kept so sprites are not perfectly static relative to each other.
            .angularVelocity = (Random01() - 0.5f) * 0.9f * (useFb ? s_smokePuffFbSpin : 1.0f),
        });
    }
}

// P2 persistent smoke source. The puff above remains the Event primary; this
// pool owns rate, fractional carry, seed and wind for sustained plumes.
#define VFX_SMOKE_EMITTER_MAX 16
typedef struct {
    bool active;
    bool stopping;
    Vector3 pos, wind;
    VC_MaterialId matId;
    float scale, density, accum, elapsed, seed;
} VC_SmokeEmitter;
static VC_SmokeEmitter s_smokeEmitters[VFX_SMOKE_EMITTER_MAX];
static int s_smokeEmitterSerial = 0;

int VFX_SmokeEmitter_Spawn(Vector3 pos, VC_MaterialId matId, float scale, float density)
{
    SmokePuff_InitShared();
    int slot = -1;
    for (int i = 0; i < VFX_SMOKE_EMITTER_MAX; ++i)
        if (!s_smokeEmitters[i].active) { slot = i; break; }
    if (slot < 0) slot = s_smokeEmitterSerial++ % VFX_SMOKE_EMITTER_MAX;
    s_smokeEmitters[slot] = (VC_SmokeEmitter){
        .active = true, .pos = pos, .matId = matId,
        .scale = scale > 0.0f ? scale : 1.0f,
        .density = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density),
        .seed = (float)slot * 2.399963f + pos.x * 0.23f + pos.z * 0.59f,
    };
    return slot;
}
void VFX_SmokeEmitter_SetTransform(int handle, Vector3 pos, Vector3 wind)
{
    if (handle < 0 || handle >= VFX_SMOKE_EMITTER_MAX || !s_smokeEmitters[handle].active) return;
    s_smokeEmitters[handle].pos = pos; s_smokeEmitters[handle].wind = wind;
}
void VFX_SmokeEmitter_SetDensity(int handle, float density01)
{
    if (handle < 0 || handle >= VFX_SMOKE_EMITTER_MAX || !s_smokeEmitters[handle].active) return;
    s_smokeEmitters[handle].density = density01 < 0.0f ? 0.0f : (density01 > 1.0f ? 1.0f : density01);
}
void VFX_SmokeEmitter_Stop(int handle) { if (handle >= 0 && handle < VFX_SMOKE_EMITTER_MAX) s_smokeEmitters[handle].stopping = true; }
void VFX_KillSmokeEmitter(int handle) { if (handle >= 0 && handle < VFX_SMOKE_EMITTER_MAX) s_smokeEmitters[handle].active = false; }
static void SmokeEmitter_Update(float dt)
{
    for (int i = 0; i < VFX_SMOKE_EMITTER_MAX; ++i) {
        VC_SmokeEmitter *e = &s_smokeEmitters[i];
        if (!e->active) continue;
        if (e->stopping) { e->active = false; continue; }
        e->elapsed += dt;
        e->accum += dt * (2.0f + 7.0f * e->density);
        int count = (int)e->accum;
        if (count > 3) { count = 3; e->accum = 0.0f; } else e->accum -= (float)count;
        for (int n = 0; n < count; ++n) {
            float phase = e->seed + e->elapsed * 1.7f + (float)n * 2.399963f;
            Vector3 p = Vector3Add(e->pos, Vector3Scale(e->wind, 0.12f));
            Vector3 orbit = VC_TangentXZ(phase, 0.0f);
            p.x += orbit.x * 0.08f * e->scale;
            p.z += orbit.z * 0.08f * e->scale;
            VFX_ComposeSmokePuff(p, e->matId, e->scale, 0.06f + 0.08f * e->density);
        }
    }
}
static void SmokeEmitter_Draw3D(Camera3D cam) { (void)cam; }
