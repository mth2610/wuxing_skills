#ifndef MESH_DEFORM_H
#define MESH_DEFORM_H

#include "core/uv/uv_deform.h" // UVEnvelopeKind — the SAME envelope, on purpose
#include "raylib.h"
#include <stdbool.h>

// -----------------------------------------------------------------------------
// MESH DEFORMATION — D(P, N, mat, t) as a stack of typed layers
//
// The vertex-space twin of core/uv/uv_deform.h. Same shape, same discipline,
// one axis apart:
//
//   core/uv/      UV' = UV + W(uv,   mat, t)   — fragment: the SAMPLING coordinate
//   core/deform/  P'  = P  + D(P, N, mat, t)   — vertex:   the GEOMETRY
//
// They share `UVEnvelopeKind` deliberately rather than by accident: the
// along-surface gate that weights a wave's amplitude, blends a texture layer,
// and scales a displacement is one concept. The reference technique this
// implements calls it GradientScale(V) and uses it to weld the smoke to the
// emitter — zero excursion at the source, growing with height. That is
// UV_ENV_HEAD_WELD.
//
// WHAT THIS IS FOR. A cylinder (or funnel, or any swept section) plus an RGB
// noise sheet plus time gives a volume that churns like smoke, without a single
// particle. The noise channels are independent fields; each drives one layer;
// the layers displace the surface along its own normal. Scroll a sheet over the
// result and it reads as smoke rather than as a pipe.
//
// TWO NOISE SOURCES, BOTH LIVE:
//   * an IMAGE (R8G8B8A8, seamless on both axes) — the technique's own source,
//     used by the trail system's tubes. Channels are independent fields, so one
//     bilinear fetch feeds one layer.
//   * a PROCEDURAL periodic lattice — no asset, used by the beam. Its period
//     around the section MUST divide the section exactly or the field leaves a
//     seam down the whole body where u = 0 meets u = 1. That constraint is why
//     `tiling.x` applies to the image source only (see MeshDeformLayer).
//
// NOTHING HERE RUNS ON THE GPU, AND THERE IS DELIBERATELY NO GLSL MIRROR YET.
// No vertex shader in this engine samples a texture, so vertex texture fetch is
// unproven on the rlvk backend; a mirror written today would be a shader nobody
// can run and nobody can verify. The layout is nonetheless GPU-shaped — the
// pack below, the vec4-only arrays, the enum-as-float — so the move is a port
// rather than a redesign when a consumer exists. See core/deform/README.md.
// -----------------------------------------------------------------------------

#define MESH_DEFORM_MAX_LAYERS 4
#define MESH_DEFORM_FLOATS_PER_LAYER 12 // 3 x vec4
#define MESH_DEFORM_PACK_FLOATS (MESH_DEFORM_MAX_LAYERS * MESH_DEFORM_FLOATS_PER_LAYER)

// Order MUST match the MESH_DEFORM_* defines in
// core/deform/shaders/mesh_deform.glsl. Append only — (float)kind is written
// straight into the packed p0.x.
typedef enum {
  MESH_DEFORM_NOISE_CHANNEL = 0, // one channel of the noise source
  MESH_DEFORM_SINE,              // periodic along the material coordinate
  MESH_DEFORM_CURL,              // three decorrelated samples -> a vector
  MESH_DEFORM_KIND_COUNT
} MeshDeformKind;

// How a layer's scalar weight becomes movement. The reference compares the
// first two side by side and they do not look alike:
//
//   NORMAL_SCALE  radius *= (1 + w)   the section breathes; the body keeps its
//                                     silhouette and reads as volume
//   NORMAL_OFFSET P += N * w          the surface is pushed off its own normal;
//                                     wider, asymmetric, more chaotic
//
// AXIS and TANGENT displace along a caller-supplied direction instead, for
// shear and for stretching along the sweep.
typedef enum {
  MESH_DEFORM_DIR_NORMAL_SCALE = 0,
  MESH_DEFORM_DIR_NORMAL_OFFSET,
  MESH_DEFORM_DIR_AXIS,
  MESH_DEFORM_DIR_TANGENT,
  MESH_DEFORM_DIR_COUNT
} MeshDeformDirection;

// ONE FLAT STRUCT, NO UNION — the ForceLayer convention. Fields are reused per
// kind rather than widened, so the packed layout never changes:
//
//   MESH_DEFORM_NOISE_CHANNEL
//     channel     which channel of the sheet (0..3); each is its own field
//     tiling.y    scale along the body
//     speed       multiplies the field's timeScale
//   MESH_DEFORM_SINE
//     tiling.y    cycles along the body
//     speed, phase as usual; `channel` unused
//   MESH_DEFORM_CURL
//     channel     the FIRST of three consecutive channels
//
// `tiling.x` (around the section) applies to the IMAGE source only. The
// procedural lattice derives its around-period from the section itself, because
// a period that does not divide the section leaves a seam running the whole
// length of the body at u = 0 / u = 1. Scaling it would break that by
// construction, so it is refused rather than silently honoured.
//
// `timeOffset` and `latticeMul` are procedural-source only and are NOT packed
// for the GPU: the GPU path always has the sheet.
typedef struct {
  MeshDeformKind kind;
  MeshDeformDirection direction;
  int channel;
  Vector2 tiling;
  float amplitude; // this layer's weight; the FIELD's amplitude scales the sum
  float speed;
  float phase;
  float timeOffset; // procedural only — decorrelates octaves in the time axis
  float latticeMul; // procedural only — period multiplier along the body
  UVEnvelopeKind env;
  float envStart;
  float envEnd;
} MeshDeformLayer;

typedef struct {
  MeshDeformLayer layers[MESH_DEFORM_MAX_LAYERS];
  int layerCount;

  // Applied ONCE to the summed weight, never folded into a layer. Keeping it
  // outside the sum is what lets a caller migrate onto this module without its
  // output changing: a*(x+y) is not a*x + a*y in float.
  float amplitude;
  float timeScale;

  // Procedural source periods. latticeAround MUST be the section's own segment
  // count — see the note on MeshDeformLayer.tiling.
  int latticeAround;
  int latticeAlong;

  // Image source. R8G8B8A8, seamless on BOTH axes. NULL selects the procedural
  // lattice. Not owned.
  const unsigned char *noisePixels;
  int noiseImgW;
  int noiseImgH;
} MeshDeformField;

typedef struct {
  // Multiplier on the section radius. 1.0 = untouched. NORMAL_SCALE layers
  // land here, so the caller applies it where it already computes its radius
  // and no silhouette information is lost.
  float radiusScale;
  // The same quantity as a bare addend: radiusScale == 1 + radiusDelta.
  //
  // Not redundant. A caller that already accumulates its own radius multiplier
  // must add this term to it, and recovering it as (radiusScale - 1) loses
  // precision whenever the term is small against 1 — which is the normal case.
  // Handing it over directly is what lets an existing consumer move onto this
  // module with its output unchanged to the last bit.
  float radiusDelta;
  // Everything else, in world units, already in the caller's frame.
  Vector3 offset;
} MeshDeformResult;

typedef enum {
  MESH_DEFORM_PRESET_TUBE_CHURN = 0, // the trail tube's two octaves, as-is
  MESH_DEFORM_PRESET_BEAM_RIPPLE,    // the beam's procedural pair, as-is
  MESH_DEFORM_PRESET_SMOKE_COLUMN,   // welded at the source, opening with height
  MESH_DEFORM_PRESET_COUNT
} MeshDeformPreset;

// --- Construction -----------------------------------------------------------
void MeshDeform_Clear(MeshDeformField *f);
bool MeshDeform_AddLayer(MeshDeformField *f, MeshDeformLayer layer); // false if full
MeshDeformField MeshDeform_CreatePreset(MeshDeformPreset preset);

// --- Evaluation -------------------------------------------------------------
// `surf` is the surface parameter pair: .x around the section (0..1, wrapping),
// .y along the body (0..1). The ENVELOPE is measured on surf.y.
//
// `mat` is the MATERIAL coordinate — the label stamped on the geometry, not a
// distance from an end that moves. Passing surf.y here instead is the mistake
// that makes a churning body read as a pre-squeezed shape being dragged: the
// bulges sit at fixed fractions of the body and travel with it. Push
// accumulated distance into mat.y and they travel THROUGH it.
//
// `normal`, `axis`, `tangent` are the caller's frame. Only the directions a
// field actually uses need to be real.
MeshDeformResult MeshDeform_Evaluate(const MeshDeformField *f, Vector2 surf,
                                     Vector2 mat, float time, Vector3 normal,
                                     Vector3 axis, Vector3 tangent);

// The raw scalar of one layer, before direction. Exposed for tests and for a
// caller that wants to route the weight itself.
float MeshDeform_EvaluateLayer(const MeshDeformField *f, const MeshDeformLayer *L,
                               Vector2 surf, Vector2 mat, float time);

// Bilinear sample of one channel, WRAPPING both axes. Wrapping is mandatory,
// not a convenience: the section is closed, so clamping leaves a seam running
// the whole length of the body exactly where u = 0 meets u = 1.
float MeshDeform_SampleImage(const unsigned char *px, int w, int h, int channel,
                             float u, float v);

// Periodic value noise on an integer lattice, hashed in place — no table, no
// allocation. Periodic on BOTH axes for the same reason.
float MeshDeform_SampleLattice(float u, float v, float w, int periodU, int periodV);

// --- GPU --------------------------------------------------------------------
// Packs MESH_DEFORM_PACK_FLOATS floats = MESH_DEFORM_MAX_LAYERS x 3 vec4,
// mirrored by core/deform/shaders/mesh_deform.glsl:
//   [i*12 +  0..3] = (kind, direction, channel, amplitude)
//   [i*12 +  4..7] = (tilingU, tilingV, speed, phase)
//   [i*12 + 8..11] = (envKind, envStart, envEnd, 0)
// and meta = (layerCount, amplitude, timeScale, 0).
//
// Zeroes first, then COMPACTS zero-amplitude layers, so GPU indices are not
// CPU indices. Procedural-source fields are not packed; see MeshDeformLayer.
void MeshDeform_PackGPU(const MeshDeformField *f, float *outFloats /*[PACK_FLOATS]*/,
                        float *outMeta /*[4]*/);

typedef struct {
  int fieldLoc;
  int metaLoc;
} MeshDeformLocs;

MeshDeformLocs MeshDeform_CacheLocations(Shader shader);

// MUST be called after BeginShaderMode(shader) for that same shader: under rlvk
// the shader argument supplies only the location, while the destination is
// whichever shader is currently bound.
void MeshDeform_Apply(const MeshDeformField *f, Shader shader,
                      const MeshDeformLocs *locs);

#endif // MESH_DEFORM_H
