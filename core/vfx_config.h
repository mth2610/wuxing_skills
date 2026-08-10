#ifndef CORE_VFX_CONFIG_H
#define CORE_VFX_CONFIG_H

typedef enum {
    VFX_BLEND_ALPHA = 0,   // DEFAULT — smoke, dust, ash: anything that occludes
    VFX_BLEND_ADDITIVE,    // embers, sparks, glow, the incandescent core of fire
    // PREMULTIPLIED — the case the blend law's binary has no answer for: a thing
    // that EMITS and OCCLUDES at once. Fire is exactly that (an incandescent
    // core seen through its own soot), and splitting it into an additive core
    // plus an alpha body is a workaround, not a model: the two populations
    // interleave in the depth sort, so every alternation costs a batch flush.
    //
    // dst = src.rgb + dst.rgb * (1 - src.a). RGB is a light contribution that
    // is INDEPENDENT of alpha, so one draw can add a blown-out core while its
    // sooty rim darkens the background. The shader must output PREMULTIPLIED
    // colour (rgb already scaled by its own coverage) or the result is doubled.
    VFX_BLEND_PREMULTIPLIED,
} VFX_BlendMode;

#include "raylib.h"
#include <stddef.h>
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/sprite_anim.h"
#include "core/skill_curve.h"
#include "core/vfx_contrast.h"

// 1. General Config
typedef struct {
    float life;      // lifetime / duration
    int priority;    // pool eviction priority (VFXPriority)
    int tag;         // ownerTag
} VFX_GeneralConfig;

// 2. Geometry Config
typedef struct {
    float scale;     // overall size scale
    float radius;    // spherical radius (for particles/boulders)
    float width;     // thickness / beam width
    float length;    // trail/ribbon length
} VFX_GeometryConfig;

// 3. Physics Config
struct ParticleConfig;
typedef struct {
    Vector3 position;
    Vector3 velocity;
    float speed;
    const ForceField *forceField;
    
    // Impact/Collision upgrades
    bool collisionEnabled;
    float collisionElasticity; // 0.0 (stick) to 1.0 (perfect bounce)
    float collisionFloorY;     // local floor level
    const struct ParticleConfig *onCollisionEmit; // sub-emitter spawned on bounce
    int onCollisionEmitCount;
} VFX_PhysicsConfig;

// 4. Animation Config
typedef struct {
    const SpriteAnim *spriteAnim;
    const SkillCurve *radiusCurve;
    const SkillCurve *speedCurve;
    const SkillCurve *alphaCurve;
    const SkillCurve *emissiveCurve;
    const SkillCurve *widthCurve;
} VFX_AnimationConfig;

// 5. Render Config
typedef struct {
    Color tint;
    Color colorStart;
    Color colorEnd;
    const ColorGradient *gradient;
    Shader shader;
    Texture2D texture;

    // Blend mode — Đợt E / F1b, THE BLEND LAW:
    //   if the thing would BLOCK light in reality, it is ALPHA and it gets lit;
    //   if it EMITS light, it is ADDITIVE and stays unlit.
    // Smoke that glows is TWO populations — an alpha body plus an additive core
    // — never one additive draw. Additive output can never be darker than its
    // background, which is why additive smoke always reads as glowing gas.
    // 0 = VFX_BLEND_ALPHA (default), so nothing already written changes.
    int blendMode;         // VFX_BlendMode

    // Emissive intensity boost for HDR bloom (1.0 = default, >1.0 = glowing core).
    // Mirrors GpuParticleConfig.emissiveBoost so the CPU and GPU particle paths
    // take the same value — the glowing core existed only on the GPU side until
    // this landed.
    //
    // The GPU path can bake the boost straight into the colour because it stores
    // colour as FLOAT. The CPU path cannot: its vertices go through rlColor4ub,
    // which is 8-bit and caps at 1.0 (rlColor4f just converts down to the same
    // thing). So here the value rides a shader uniform instead, and it takes part
    // in the draw batching the way `unlit` does — particles with different boosts
    // land in different batches.
    float emissiveBoost;

    // EMISSIVE opt-out. Lighting is a MULTIPLY, so anything that emits its own
    // light must not go through it — a flame body multiplied by a dim night sky
    // turns brown, which is exactly what happened when F1's lighting was applied
    // globally. Smoke/dust/ash occlude and want lighting; fire, sparks, glow and
    // magic do not. 0 = lit (default, so existing effects are unaffected).
    int unlit;

    // ── PACKED VOLUME SHEET (the 4-channel flipbook) ─────────────────────────
    //
    // 1 = this particle's texture is a packed volume sheet from
    // scripts/flipbook/ (R = flame emission, G = smoke density, B = self-shadow,
    // A = true opacity) and the shader must decode all four channels instead of
    // reading RGB as colour. 0 = legacy `texel * vertexColour` (default).
    //
    // WHY: the legacy path multiplies the WHOLE sprite by one vertex colour, so
    // every texel shares a hue — a flame can have a bright centre but never a
    // WHITE-hot core with an orange rim, which is most of what makes fire read
    // as fire. In volume mode the sheet's R is a per-texel TEMPERATURE and the
    // colour comes from `rampLUT` below, so the zoning is per pixel.
    //
    // The sheet stays greyscale on purpose. Hue lives in the LUT, so the same
    // fire sheet becomes purple/blue magic fire by swapping the ramp — baking
    // colour into the texture would forfeit that.
    int volumeSheet;

    // Black-body (or magic) ramp for `volumeSheet`, baked with
    // ColorGradient_BakeLUT. id 0 = fall back to the flat vertex colour, so a
    // missing LUT degrades to the legacy look instead of drawing black.
    // Part of the draw batch key, like `unlit` — one ramp per batch.
    Texture2D rampLUT;

    // Multiplier on the sheet's emission before it indexes the ramp. This is
    // the exposure knob for "how much of the sprite is white-hot": the sim
    // normalises emission to its own 99.5th percentile, which has no idea how
    // bright this particular effect should read. 0 = treat as 1.0.
    float heatGain;

    // Shared contrast policy. NONE (0) is identity for every legacy effect;
    // compositions select a semantic profile instead of hand-tuning each
    // particle/ribbon/decal renderer independently.
    VFXContrastProfileId contrastProfile;

    // Velocity stretch
    float stretchStrength; // 0.0 = disabled (default)
    float stretchMinSpeed; // speed threshold to apply stretch

    // Particle Ribbon trails
    int trailLength;       // 0 = disabled (default), Max = 8
    float trailWidthRatio; // trail width factor relative to particle radius
    Color trailColorStart;
    Color trailColorEnd;
    // Seconds between recorded trail points. 0 = the legacy 0.015 s.
    //
    // The tail's LENGTH is trailLength * trailStepTime * speed, and trailLength
    // is capped at 8 by the static history buffer — so at the legacy step a tail
    // can only ever cover 0.105 s of travel, which is a comet dash, never a
    // flowing wisp. Raising the step buys length with no extra memory; the cost
    // is a coarser polyline, which is invisible on a smooth path and shows up as
    // faceting on a sharply curving one.
    float trailStepTime;
    // 1 = do not draw the particle's own billboard at all; only its ribbon
    // trail. The particle becomes a PATH rather than a sprite — a thin wisp of
    // gas with no head. Setting the head's alpha to 0 cannot do this (the trail
    // scales its own alpha by the particle's, so the trail vanishes with it),
    // and shrinking the head to nothing also thins the trail, whose half-width
    // is radius * trailWidthRatio. `radius` still sets the wisp's thickness.
    int trailOnly;
    // 1 = subdivide the trail with a Catmull-Rom through the recorded points
    // instead of drawing the raw polyline. History is only 8 points spaced
    // trailStepTime apart, so a curving path shows its corners as facets — a
    // bent wire rather than a thread of gas. 0 = the legacy polyline.
    int trailSmooth;

    // Noise/Flow Distortion for Trails & Ribbons
    float distortionStrength; // 0.0 = disabled (default)
    float distortionSpeed;    // time multiplier for distortion noise
} VFX_RenderConfig;

#endif // CORE_VFX_CONFIG_H
