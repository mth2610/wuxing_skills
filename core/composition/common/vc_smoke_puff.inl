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

    // Grows to ~2.2x over its life and never shrinks back — smoke does not
    // contract, it dissipates. The fade curve is what removes it.
    FloatCurve_AddStop(&s_smokePuffGrow, 0.0f, 0.45f);
    FloatCurve_AddStop(&s_smokePuffGrow, 0.25f, 1.0f);
    FloatCurve_AddStop(&s_smokePuffGrow, 1.0f, 2.2f);

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
    int count = (int)(SMOKE_PUFF_MAX_SPRITES * density * s_smokePuffCountMul);
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
                               powf(Random01(), 1.8f)) * scale,
            .lifetime = Math_Mix(1.1f, 2.0f, Random01()),
            .colorStart = VC_WithAlpha(c, (unsigned char)(255.0f *
                              (s_smokePuffAlpha < 0.0f ? 0.0f :
                               s_smokePuffAlpha > 1.0f ? 1.0f : s_smokePuffAlpha))),
            .colorEnd = VC_WithAlpha(c, 0),
            .forceField = &s_smokePuffFld,
            .radiusCurve = &s_smokePuffGrow,
            .alphaCurve = &s_smokePuffFade,
            // Per-sprite spin. Without this the repeated texture is obvious no
            // matter how many sprites are stacked.
            .render.texture = s_smokePuffTex[i % SMOKE_PUFF_VARIANTS],
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * 0.9f,
        });
    }
}
