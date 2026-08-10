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
//
// WHY THIS EXISTS. The three things it replaces were three hand-written shader
// modes plus three private style tables, so "a new trail" meant a new branch and
// a fix landed in one copy of three. Two bugs on 10/08/2026 came straight out of
// that shape: a stale style-override global silently swapped an entire style row
// (the description was not owned by the trail), and an emission weight was
// reused as alpha coverage in two duplicated layer loops. A recipe is DATA, so a
// new trail is a row in `k_trailPresets[]`, and there is exactly one formula to
// fix when one is wrong.
//
// EVERY FIELD IS SHARED. If you find yourself adding a field that only means
// something for one preset, you are re-growing the mode switch — express it
// through the deform/flow layers instead, which is what they are for.
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"

#include "core/trails/trail_system.h" // TrailLayer, TrailWidthEnvelopeType
#include "core/uv/surface_flow.h"
#include "core/uv/uv_deform.h"
#include "core/vfx_contrast.h"
#include "core/vfx_surface_registry.h"

// ── How the deform layers turn into samples ─────────────────────────────────
// NOT a mode: both routes run over the SAME uniform blocks and the same shared
// GLSL (`core/uv/shaders/uv_field.glsl` provides both entry points), and every
// other field of the recipe means the same thing under either. This is the one
// genuine authoring choice the reference decomposition itself calls out — see
// that file's "SUM OR PARALLEL: BOTH, AND THEY ARE NOT INTERCHANGEABLE".
typedef enum {
    // Every deform layer sums into ONE warped coordinate, then the flow layers
    // sample it. A warped sheet: fire wisps, smoke, anything whose silhouette
    // is painted into the texture.
    TRAIL_SAMPLE_SUMMED = 0,
    // Each deform layer carries its OWN sample of the sheet, combined with the
    // flow's blend op. Braided strand bundles: the gaps between the samples are
    // the effect, and summing the layers would fill them back in and give the
    // single smooth band this route exists to avoid.
    TRAIL_SAMPLE_PARALLEL,
    TRAIL_SAMPLE_COUNT
} TrailSampleTopology;

// ── What erases parts of the surface ────────────────────────────────────────
typedef struct {
    // Softness of the mask at the strip's two long edges, as a fraction of the
    // half-width. The hard clamp the deform layers swing inside: nothing
    // survives past the quad boundary whatever the waves did.
    float edgeSoft;
    // Dissolve threshold read from the sheet's dissolve channel, and the width
    // of its transition. The threshold climbs toward the tail so the head stays
    // whole and the end erodes into islands instead of dimming evenly.
    float dissolve;
    float dissolveSoft;
    // Segment-space fade. tailFadeA >= tailFadeB disables it.
    float tailFadeA, tailFadeB;
    // ── HOW THE TAIL COMES APART ────────────────────────────────────────────
    // A tail whose every layer stops at the same segment reads as clipped
    // however soft the fade over it — softness blurs a cut, it does not remove
    // one. These three make it unravel.
    float tailStagger;  // spread between the layers' end points, in seg space
    float tailDissolve; // extra dissolve over the dying stretch
    float tailNarrow;   // layer half-width multiplier at the tip (1 = none)
} TrailMaskConfig;

// ── How densely the sheet's authored hairs are read across the strip ────────
//
// THIS IS WHERE AN ARCHETYPE LIVES. One strand sheet can read as a braid of
// filaments, as a sharp-edged blade, as low-frequency cloth or as a formless
// dim mass — and 10/08/2026's sweep showed the difference is entirely these
// numbers, not the image. `gain` is the load-bearing one: it is the exponent on
// the sampled density, so below 1 it lifts the gaps until the hairs merge into
// a continuous body, and above 1 it pushes them apart into separate filaments.
// A "mass" sheet was scoped before that was measured; it is not needed.
//
// These five used to be INFERRED by the legacy bridge from fields that have
// nothing to do with them — the third bundle's weight from the tail's narrowing,
// the flow distortion from the dissolve's softness. Those couplings were
// invented to avoid a hardcode and are not real, so a change to a tail knob
// silently re-sampled the surface. Authored per preset, they cannot.
typedef struct {
    // Half-width of ONE strand bundle, as a fraction of the strip half-width.
    // Three bundles ride across the quad at this width; below ~0.2 they cover a
    // quarter of the strip and the trail reads as a hairline whatever its
    // radius, which is why every swept preset looked too thin.
    float bundleWidth;
    // pow() exponent on the sampled density. < 1 fills, > 1 separates. 0 is not
    // a valid value — the shader clamps it to 0.05.
    float gain;
    float fineMix;     // weight of the sheet's fine (G) bundle
    float thirdWeight; // weight of the widest (third) bundle
    float flowDistort; // B-channel distortion strength
} TrailStrandConfig;

// ── What colour the surviving parts are ─────────────────────────────────────
typedef struct {
    // The along-trail ramp. The head takes the entity tint; this is the far
    // end. Without it the whole ribbon is one flat hue and reads as a smear
    // rather than as something cooling or thinning out. Leave black to disable.
    Color tail;
    // The hot centre. Structural, NOT a threshold in the sheet: the authored
    // hairs need not cross a bundle's exact centre, and keying the core off
    // texture density made it vanish wherever the sheet happened to be dark
    // while the broad support stayed visible.
    Color hot;
    // Half-width of that centre, in the same units the deform layers swing in
    // (fraction of the strip half-width). 0 disables the core entirely, which
    // is correct for anything with no deform layers to have a centreline.
    float coreWidth;
    // How hard the core burns toward `hot`. The BODY pass takes a narrower,
    // lower-energy copy so its hue stays readable on bright maps; EMISSION
    // takes the full HDR one.
    float coreIntensity;
    // How far the tail cools toward the element's own dark body tone. 0 = the
    // tail keeps the head colour. Toward the material's dark tone rather than
    // toward black: a trail that ramps to black reads as dirt, not as cooling.
    float tailDarken;
    // TWO ENCODINGS OF ONE IDEA, and the general one is the gradient. true =
    // take the element's authored N-stop ramp (VC_ElementRamp) along the trail;
    // false = build a 2-stop ramp at spawn from the material tint and
    // `tailDarken`. A 2-stop ramp IS a gradient, so this is a shortcut, not a
    // second concept — but the shortcut is what lets a preset cool toward a
    // computed tone instead of an authored one.
    bool useElementRamp;
    // Contrast policy shared with particles, ribbons and decals.
    VFXContrastProfileId contrast;
} TrailColorConfig;

// ── Where the head colour comes from ────────────────────────────────────────
// Was a bool (glow or body). A bool cannot say NEUTRAL, and smoke needs to:
// it is a combustion PRODUCT, not the element, so tinting it with the element's
// body colour makes fire's smoke red and lightning's smoke blue. The element
// belongs at the head of a HOT trail; a plume carries no element identity at all.
typedef enum {
    TRAIL_TINT_GLOW = 0, // the element's hot tone — energy, sparks, blades
    TRAIL_TINT_BODY,     // the element's identity tone — coloured matter
    TRAIL_TINT_NEUTRAL,  // element-independent; the material only shapes it
    TRAIL_TINT_COUNT
} TrailTintSource;

// ── The recipe ──────────────────────────────────────────────────────────────
// Tagged so trail_system.h can forward-declare it: the recipe includes that
// header (for TrailLayer), so the dependency can only run one way.
typedef struct TrailRecipe {
    // Geometry. RIBBON_FIXED_NORMAL lies in a plane (a blade: broad from the
    // front, edge-on from the side); RIBBON_CAMERA_FACING never pinches on a
    // curve. Stored as int so this header does not have to pull in the ribbon
    // primitive — the composer maps it.
    int   ribbonMode;
    Vector3 fixedNormal;      // only read for RIBBON_FIXED_NORMAL

    VFX_SurfaceId surface;    // the sheet; its channel grammar is normative
    // Runtime-generated sheet, when a preset's surface is built in code rather
    // than authored on disk (the swept presets' procedural streak mask). NULL =
    // resolve `surface` through the registry, which is the normal case.
    // Composition never owns a FILENAME either way.
    const Texture2D *sheetOverride;

    // ── EXTRA GEOMETRY, and why this is not a flow layer ────────────────────
    // A wider soft halo around a textured body is a WIDER QUAD (widthMul 1.3 to
    // 1.65). One draw has one quad, so this cannot be folded into the flow
    // layers however much tidier that would look — flow layers re-sample one
    // surface, they do not widen it. NULL/0 = a single strip, which is what the
    // strand presets want (their waves swing inside one quad and the edge mask
    // is the silhouette).
    //
    // A layer's `alphaMul` is an EMISSION WEIGHT: the layers sum as light. It is
    // NOT coverage, and the renderer must not reuse it as such — that confusion
    // is what made a trail wash out over bright scenery. `bodyOpacity` below is
    // the coverage, and trail_system.c's TrailLayerPassAlphaMul is what keeps
    // the two apart.
    const TrailLayer *layers;
    int layerCount;

    TrailSampleTopology topology;
    UVDeformField deform;     // the warp
    SurfaceFlow   flow;       // the sample

    TrailStrandConfig strand; // how the sheet is read (the archetype)
    TrailMaskConfig  mask;
    TrailColorConfig colour;

    // ── THE PASS SPLIT ──────────────────────────────────────────────────────
    // Two separately authored numbers, and they must stay separate. Additive
    // radiance can only ADD, so over a bright destination it pushes toward
    // white and loses hue; the BODY pass is what holds colour there. Reusing
    // one number for both is the bug that made the ribbon trail wash out.
    //   bodyOpacity 0   = pure emissive (correct for sparks and glints)
    //              ~0.5 = a coloured core inside a glow
    //              ~0.9 = smoke, which must occlude what is behind it
    float bodyOpacity;
    float hdrGain;            // EMISSION lift; > 1 is what bloom catches

    // Width envelope along the strip, and how opaque the sheet's own alpha is
    // allowed to get. Kept here rather than in the simulation because they are
    // look decisions, not motion ones.
    float radiusDefault;      // metres, when the caller passes 0
    unsigned char tintAlpha;
    TrailTintSource tintSource;
    bool additive;            // false = the trail occludes instead of glowing
} TrailRecipe;

// ── Presets ─────────────────────────────────────────────────────────────────
// Rows of `k_trailPresets[]` in core/composition/common/vc_trail.inl. These
// replace VFX_RIBBON_* and VFX_STRAND_*, which were two enums over two style
// tables that described the same thing twice.
typedef enum {
    TRAIL_PRESET_BLADE = 0, // swept blade: broad in the swing plane, edge-on side
    TRAIL_PRESET_MAIN,      // the general camera-facing energy wake
    TRAIL_PRESET_WISP,      // thinner, continuous inner core, softer
    TRAIL_PRESET_BACKDROP,  // wide dim underlay meant to sit behind another trail
    TRAIL_PRESET_ENERGY,    // braided hot filaments with a gold core
    TRAIL_PRESET_SMOKE,     // many faint strands piling into occluding mass
    TRAIL_PRESET_COUNT      // range-check against THIS, never the last by name
} TrailPresetId;

#endif // CORE_TRAIL_RECIPE_H
