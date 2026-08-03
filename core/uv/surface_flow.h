#ifndef SURFACE_FLOW_H
#define SURFACE_FLOW_H

#include "core/uv/uv_deform.h"
#include "raylib.h"
#include <stdbool.h>

// -----------------------------------------------------------------------------
// SURFACE FLOW — layered sampling over a (possibly deformed) coordinate
//
// The sampling half of `mesh + UVDeformField + SurfaceFlow = effect`.
// core/uv/uv_deform.h warps the coordinate; this decides where each texture
// layer reads and how the layers combine. Warp first, then sample.
//
// This is the N-layer generalisation of core/uv/flow_map.h, which stays as the
// one-layer convenience (and still owns the Valve two-phase sampler this
// builds on, FlowMap_SampleTwoPhase in core/uv/shaders/flow_map.glsl). A
// SurfaceFlow with one two-phase layer is exactly a FlowMap.
//
// It reuses UVEnvelopeKind from uv_deform.h on purpose: the along-surface gate
// that weights a wave's amplitude is the SAME weight that blends a texture
// layer. That shared envelope is why the two halves are one module.
// -----------------------------------------------------------------------------

#define SURFACE_FLOW_MAX_LAYERS 4
#define SURFACE_FLOW_FLOATS_PER_LAYER 8 // 2 x vec4
#define SURFACE_FLOW_PACK_FLOATS                                               \
  (SURFACE_FLOW_MAX_LAYERS * SURFACE_FLOW_FLOATS_PER_LAYER)

// MUST match the SURFACE_FLOW_* defines in core/uv/shaders/surface_flow.glsl.
// Append only — (float)blend is written straight into the packed p1.x.
typedef enum {
  SURFACE_FLOW_MUL = 0, // masking
  SURFACE_FLOW_ADD,     // genuinely additive energy
  // MAX is the one to reach for when layers are overlapping strands or hairs:
  // summing them fills the gaps between them back in, and the gaps ARE the
  // effect.
  SURFACE_FLOW_MAX,
  SURFACE_FLOW_BLEND_COUNT
} SurfaceFlowBlend;

typedef struct {
  Vector2 tiling; // repeats per unit of the material coordinate
  Vector2 pan;    // scroll speed, units per second
  SurfaceFlowBlend blend;
  UVEnvelopeKind env; // gates this layer's contribution
  float envStart;
  float envEnd;
} SurfaceFlowLayer;

typedef struct {
  SurfaceFlowLayer layers[SURFACE_FLOW_MAX_LAYERS];
  int layerCount;

  // SHAPE (stretch the sheet across the surface exactly once) vs MATERIAL
  // (tile it by metres). Keep this equal to the UVDeformField's stretchUV that
  // shares the surface — they describe the same asset.
  bool stretchUV;

  // Which component of the material coordinate every envelope is measured
  // along (0 = .x, 1 = .y). A whole-surface property, not a per-layer one:
  // "along the surface" means the same axis for every layer on it.
  int envAxis;

  // Valve two-phase sampling for layer 0. Cross-fades two copies of the layer
  // at opposite phases so the texture never stretches past half a cycle.
  bool twoPhase;
  float twoPhaseSpeed;
  float twoPhaseStrength;
} SurfaceFlow;

// --- Construction -----------------------------------------------------------
void SurfaceFlow_Clear(SurfaceFlow *sf);
bool SurfaceFlow_AddLayer(SurfaceFlow *sf, SurfaceFlowLayer layer); // false if full

// --- CPU mirror of the GLSL -------------------------------------------------
// Transliterations of core/uv/shaders/surface_flow.glsl. See
// core/tests/uv_deform_test.c for the mirror guard.

// A scroll offset folded to [0,1). Exact for a REPEAT-wrapped sampler and for
// a sine, because both are periodic with the period this folds to. NOT exact
// for an aperiodic consumer: fold the time term of a value-noise domain and
// the field jumps once per second. Ask what the coordinate feeds first.
float SurfaceFlow_Pan(float t, float speed);

// The along-surface coordinate: tile by a material measure, or stretch once.
// `base` arrives UNFOLDED and is folded here exactly once, together with this
// layer's own scale — fract(fract(base) * scale) is not fract(base * scale),
// and folding before the scale changes the effective tiling rate.
float SurfaceFlow_AlongV(float stretched, float base, float scale, float pan,
                         bool stretch);

// Remap the across-surface coordinate into a band of half-width `halfWidth`
// centred on `centre` (normally a wave offset from uv_deform, so the texture
// rides the swung centreline). The caller still owns the wrap window: a sheet
// has to REPEAT to tile, so a band that has swung past this fragment will
// smear its content in from the opposite edge unless it is masked out.
float SurfaceFlow_AcrossU(float across, float centre, float halfWidth);

// One layer's sampling uv. In STRETCH mode the uv passes through untiled —
// stretching means mapping the sheet across the surface exactly once.
Vector2 SurfaceFlow_LayerUV(const SurfaceFlow *sf, int layer, Vector2 uv,
                            Vector2 mat, float t);

// --- GPU --------------------------------------------------------------------
// Packs into SURFACE_FLOW_PACK_FLOATS floats = SURFACE_FLOW_MAX_LAYERS x 2
// vec4, mirrored by core/uv/shaders/uv_field.glsl:
//   [i*8 + 0..3] = (tilingU, tilingV, panU, panV)   <- SurfaceFlow_LayerUV's tilePan
//   [i*8 + 4..7] = (blendOp, envKind, envStart, envEnd)
// and meta = (layerCount, stretchUV, envAxis, 0).
//
// Zeroes first, then COMPACTS: a layer with both tiling components zero is
// skipped, so GPU indices are not CPU indices.
//
// `pan` is packed already multiplied by `t` and folded, so the shader receives
// a ready offset in [0,1) rather than an unbounded clock — the fold happens on
// the C side where the modulus can be chosen deliberately.
void SurfaceFlow_PackGPU(const SurfaceFlow *sf, float t, float *outFloats,
                         float *outMeta /*[4]*/);

typedef struct {
  int layerLoc;
  int metaLoc;
  int twoPhaseLoc; // (speed, strength, enabled, 0)
} SurfaceFlowLocs;

SurfaceFlowLocs SurfaceFlow_CacheLocations(Shader shader);

// Sets the packed uniforms on `shader`. MUST be called after
// BeginShaderMode(shader) for that same shader — under rlvk the shader
// argument only supplies the location, the destination is whatever is bound.
// Does NOT bind any texture: the caller owns its samplers, so this cannot
// collide with a slot the caller is already using.
void SurfaceFlow_Apply(const SurfaceFlow *sf, Shader shader,
                       const SurfaceFlowLocs *locs, float t);

#endif // SURFACE_FLOW_H
