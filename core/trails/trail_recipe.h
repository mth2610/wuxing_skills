#ifndef CORE_TRAIL_RECIPE_H
#define CORE_TRAIL_RECIPE_H

// ─────────────────────────────────────────────────────────────────────────────
// TRAIL RECIPE — the whole description of what a trail LOOKS like.
//
// A trail is: a laid path (the simulation, which this file knows nothing about)
// wearing a surface. This struct is the surface, and it is assembled from the
// modules that already own each half, per `core/uv/README.md`'s
// `mesh + UVDeformField + SurfaceFlow = effect`:
//
//   geometry  core/ribbon_strip.h   — the strip the simulation lays down
//   deform    core/uv/uv_deform.h   — WARP the coordinate (waves, noise, swirl)
//   flow      core/uv/surface_flow.h— SAMPLE it (tiling, pan, layer blend)
//   texture   core/vfx_surface_registry.h — the sheet + its channel grammar
//   policy    core/vfx_contrast.h   — how BODY and EMISSION each resolve
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"

#include "core/trails/trail_system.h" // TrailLayer, TrailWidthEnvelopeType
#include "core/uv/surface_flow.h"
#include "core/uv/uv_deform.h"
#include "core/vfx_contrast.h"
#include "core/vfx_surface_registry.h"

// ── How the deform layers turn into samples ─────────────────────────────────
typedef enum {
    TRAIL_SAMPLE_SUMMED = 0,
    TRAIL_SAMPLE_PARALLEL,
    TRAIL_SAMPLE_COUNT
} TrailSampleTopology;

// ── What erases parts of the surface ────────────────────────────────────────
typedef struct {
    float edgeSoft;
    float dissolve;
    float dissolveSoft;
    float tailFadeA, tailFadeB;
    float tailStagger;  // spread between the layers' end points, in seg space
    float tailDissolve; // extra dissolve over the dying stretch
    float tailNarrow;   // layer half-width multiplier at the tip (1 = none)
} TrailMaskConfig;

// ── How densely the sheet's authored hairs are read across the strip ────────
typedef struct {
    float bundleWidth;
    float gain;
    float fineMix;     // weight of the sheet's fine (G) bundle
    float thirdWeight; // weight of the widest (third) bundle
    float flowDistort; // B-channel distortion strength
} TrailStrandConfig;

// ── What colour the surviving parts are ─────────────────────────────────────
typedef struct {
    Color tail;
    Color hot;
    float coreWidth;
    float coreDensityGate;
    float coreIntensity;
    float tailDarken;
    bool useElementRamp;
    VFXContrastProfileId contrast;
} TrailColorConfig;

// ── Where the head colour comes from ────────────────────────────────────────
typedef enum {
    TRAIL_TINT_GLOW = 0, // the element's hot tone — energy, sparks, blades
    TRAIL_TINT_BODY,     // the element's identity tone — coloured matter
    TRAIL_TINT_NEUTRAL,  // element-independent; the material only shapes it
    TRAIL_TINT_COUNT
} TrailTintSource;

// ── The recipe ──────────────────────────────────────────────────────────────
typedef struct TrailRecipe {
    int   ribbonMode;
    Vector3 fixedNormal;      // only read for RIBBON_FIXED_NORMAL

    VFX_SurfaceId surface;    // the sheet; its channel grammar is normative
    const Texture2D *sheetOverride;

    const TrailLayer *layers;
    int layerCount;

    TrailSampleTopology topology;
    UVDeformField deform;     // the warp
    SurfaceFlow   flow;       // the sample

    TrailStrandConfig strand; // how the sheet is read (the archetype)
    TrailMaskConfig  mask;
    TrailColorConfig colour;

    // ── THE PASS SPLIT ──────────────────────────────────────────────────────
    float bodyOpacity;
    float hdrGain;            // EMISSION lift; > 1 is what bloom catches

    float radiusDefault;      // metres, when the caller passes 0
    unsigned char tintAlpha;
    TrailTintSource tintSource;
    bool additive;            // false = the trail occludes instead of glowing

    // ── DEFORM & MATERIAL ENHANCEMENTS (WebGL reference) ───────────────────
    float style;              // 0 = strand default, 1..16 = WebGL specialized styles
    Color colRamp[4];         // 4-stop gradient ramp (C0, C1, C2, C3)
    Vector4 p0;               // uP0 shader parameter vector
    Vector4 p1;               // uP1 shader parameter vector
    Vector4 p2;               // uP2 shader parameter vector
    Vector4 wave;             // uWave: amp, freq, speed, agePow
    Vector4 noise;            // uNoise: amp, freq, speed, agePow
    Vector4 helix;            // uHelix: radius, freq, speed, phase
    Vector4 lane;             // uLane: offset, scale, freq, speed
    Vector2 cap;              // uCap: headRound, tailEase
    Vector2 uvScale;          // uUVScale: x, y
    float seed;               // uSeed
    float maxDist;            // uMaxD (0 = infinite)
    float add;                // uAdd (1.0 = additive, 0.0 = alpha blend)
    float alpha;              // uAlpha multiplier
    float rollRate;           // rad/m along path (screw / axial twist)
    float rollSpeed;          // rad/s spin rate
    Vector3 drift;            // directional acceleration (m/s2 * seg^2)
    Vector4 jitter;           // x = amp, y = freq, z = quantized Hz, w = head clamp
    Vector4 widthProf;        // x = taperPow, y = headLen, z = growAge, w = bulgeMid
    Vector2 shard;            // x = shardFreq, y = shardDepth
} TrailRecipe;

// ── Presets ─────────────────────────────────────────────────────────────────
typedef enum {
    TRAIL_PRESET_BLADE = 0,
    TRAIL_PRESET_MAIN,
    TRAIL_PRESET_WISP,
    TRAIL_PRESET_BACKDROP,
    TRAIL_PRESET_ENERGY,
    TRAIL_PRESET_SMOKE,
    TRAIL_PRESET_MAGIC,
    TRAIL_PRESET_WATER,

    // ── Xianxia / Martial Arts Motion Trail Presets (from WebGL reference) ──
    TRAIL_PRESET_SWORD_COMET,
    TRAIL_PRESET_SILK,
    TRAIL_PRESET_WOOD_SPIRAL,
    TRAIL_PRESET_LIGHTNING,
    TRAIL_PRESET_FIRE_JET,
    TRAIL_PRESET_DRAGON,
    TRAIL_PRESET_INK,
    TRAIL_PRESET_TALISMAN,
    TRAIL_PRESET_ICE_SHARD,
    TRAIL_PRESET_WIND_BLADE,
    TRAIL_PRESET_CROSS_BEAM,
    TRAIL_PRESET_DEMONIC,
    TRAIL_PRESET_TAIJI,
    TRAIL_PRESET_GALAXY,
    TRAIL_PRESET_COUNT,

    MOTION_RIBBON_ENERGY_SILK = TRAIL_PRESET_ENERGY,
    MOTION_RIBBON_SMOKE_WISP = TRAIL_PRESET_SMOKE,
    MOTION_RIBBON_BLADE = TRAIL_PRESET_BLADE,
    MOTION_RIBBON_EMBER_FILAMENT = TRAIL_PRESET_BLADE,
    MOTION_RIBBON_WATER_STREAM = TRAIL_PRESET_WATER,
    MOTION_RIBBON_SWORD_COMET = TRAIL_PRESET_SWORD_COMET,
    MOTION_RIBBON_SILK = TRAIL_PRESET_SILK,
    MOTION_RIBBON_WOOD_SPIRAL = TRAIL_PRESET_WOOD_SPIRAL,
    MOTION_RIBBON_LIGHTNING = TRAIL_PRESET_LIGHTNING,
    MOTION_RIBBON_FIRE_JET = TRAIL_PRESET_FIRE_JET,
    MOTION_RIBBON_DRAGON = TRAIL_PRESET_DRAGON,
    MOTION_RIBBON_INK = TRAIL_PRESET_INK,
    MOTION_RIBBON_TALISMAN = TRAIL_PRESET_TALISMAN,
    MOTION_RIBBON_ICE_SHARD = TRAIL_PRESET_ICE_SHARD,
    MOTION_RIBBON_WIND_BLADE = TRAIL_PRESET_WIND_BLADE,
    MOTION_RIBBON_CROSS_BEAM = TRAIL_PRESET_CROSS_BEAM,
    MOTION_RIBBON_DEMONIC = TRAIL_PRESET_DEMONIC,
    MOTION_RIBBON_TAIJI = TRAIL_PRESET_TAIJI,
    MOTION_RIBBON_GALAXY = TRAIL_PRESET_GALAXY
} TrailPresetId;

#endif // CORE_TRAIL_RECIPE_H
