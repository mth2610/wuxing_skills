#ifndef UV_DEFORM_H
#define UV_DEFORM_H

#include "raylib.h"
#include <stdbool.h>

// -----------------------------------------------------------------------------
// UV-SPACE DEFORMATION — W(uv, t) as a stack of typed layers
//
// The coordinate half of `mesh + UVDeformField + SurfaceFlow = effect`. This
// module warps a coordinate; core/uv/surface_flow.h samples with the warped
// result. Warp first, then sample.
//
// Modelled on core/force_field.h: a flat layer struct, a fixed-size array with
// a count, presets built from designated-initializer AddLayer calls, one
// Evaluate entry point, and a GPU pack whose byte layout is mirrored by hand
// in GLSL. The GLSL mirror is core/uv/shaders/uv_deform.glsl (pure functions)
// and core/uv/shaders/uv_field.glsl (the packed uniform block).
//
// Two ways to consume a field, and they are not interchangeable:
//   SUMMED   — UVDeform_Evaluate / UVDeform_ApplyField. Every layer displaces
//              one coordinate. This is the reference decomposition and what a
//              warped flat quad wants.
//   PARALLEL — UVDeform_EvaluateLayer / UVDeform_ApplyFieldLayer. Each layer
//              carries its own sample of the sheet and the samples are
//              combined afterwards (normally with max). This is what makes a
//              trail read as braided strand bundles; summing those layers
//              gives back the single smooth band the mode exists to avoid.
//
// THE DRIVE COORDINATE IS NOT THE UV. Every entry point takes the driving
// "material" coordinate separately from the uv being warped, because a scroll
// built on a coordinate measured from a MOVING end of the geometry reads as
// frozen however fast it scrolls. Pass a label stamped on the geometry when it
// was created — metres of emitter path at the moment a node was laid — not a
// distance from the tail. See core/docs/LANDMINES.md "A scroll built on a
// MOVING origin is the motion, not a scroll".
// -----------------------------------------------------------------------------

#define UV_DEFORM_MAX_LAYERS 4
#define UV_DEFORM_FLOATS_PER_LAYER 12 // 3 x vec4
#define UV_DEFORM_PACK_FLOATS (UV_DEFORM_MAX_LAYERS * UV_DEFORM_FLOATS_PER_LAYER)

// NOTE: this enum's order MUST match the UV_DEFORM_* defines in
// core/uv/shaders/uv_deform.glsl (GLSL cannot include a C header, so the two
// are kept in sync by hand). Adding a kind MUST append at the end — never
// insert in the middle or renumber, because (float)kind is written straight
// into the packed p0.x.
typedef enum {
  UV_DEFORM_SINE = 0,   // periodic; the reference's Sin01/02/03
  UV_DEFORM_NOISE_FLOW, // two zero-mean samples at different pans, added
  UV_DEFORM_FLOWMAP,    // RG texture read, decoded to [-1,1]
  UV_DEFORM_SWIRL,      // rotate about a centre, angle falling off with radius
  UV_DEFORM_KIND_COUNT  // range-check against THIS, never the last kind by name
} UVDeformKind;

// Same hand-sync contract as UVDeformKind (UV_ENV_* in uv_deform.glsl).
typedef enum {
  UV_ENV_NONE = 0,
  UV_ENV_RAMP,       // linear 0->1 across [start, end], clamped
  UV_ENV_BELL,       // raised cosine: zero value AND zero slope at both ends
  UV_ENV_SMOOTHSTEP, // the GLSL builtin
  UV_ENV_HEAD_WELD,  // smoothstep(start, end, c) * c — excursion exactly 0 at c=0
  // Same weld, but c^2 — the reference's S(V) = V^p with p > 1.
  //
  // HEAD_WELD is p = 1: past `end` the smoothstep saturates and what remains is
  // exactly c. A linear ramp spreads the excursion evenly over the whole body,
  // which is right for welding a trail to its emitter and wrong for anything
  // that is supposed to stay tight at the source and open out near the far end.
  // A rising column is the second case: p = 1 gave a body that churned as hard
  // at knee height as at the top.
  UV_ENV_HEAD_WELD_SQ,
  UV_ENV_KIND_COUNT
} UVEnvelopeKind;

// ONE FLAT STRUCT, NO UNION. Fields are reused per kind rather than widened,
// so the packed GPU layout never changes (the ForceLayer convention):
//
//   UV_DEFORM_SINE
//     frequency  cycles per unit of the drive coordinate
//     speed      turns per second
//     phase      radians
//     param      phase MULTIPLIER used by UVDeform_SetPhase (see below); the
//                shader never reads it for this kind
//   UV_DEFORM_NOISE_FLOW
//     amplitude  scales the summed zero-mean pair; frequency/speed/phase are
//                the caller's business, since the caller takes the samples
//     param      unused, keep 0
//   UV_DEFORM_FLOWMAP
//     frequency  tiling of the flow lookup
//     speed, phase, param  unused, keep 0
//   UV_DEFORM_SWIRL
//     amplitude  peak rotation in radians
//     frequency  radial falloff (0 = rigid rotation)
//     phase      centre X   \  the swirl centre; SWIRL warps the coordinate
//     param      centre Y   /  itself, so it ignores driveAxis/outAxis
typedef struct {
  UVDeformKind kind;
  int driveAxis; // 0 = material.x, 1 = material.y
  int outAxis;   // 0 = displace u, 1 = displace v
  float amplitude;
  float frequency;
  float speed;
  float phase;
  float param;
  UVEnvelopeKind env;
  int envAxis; // 0 = material.x, 1 = material.y
  float envStart;
  float envEnd;
} UVDeformLayer;

typedef struct {
  UVDeformLayer layers[UV_DEFORM_MAX_LAYERS];
  int layerCount;
  // SHAPE (stretch the sheet across the surface exactly once) vs MATERIAL
  // (tile it by metres). An authoring fact about the asset, not something any
  // code can infer: tiling a sheet whose head and tail taper are painted in
  // gives a rope of identical segments with no silhouette. Carried in the
  // field so the deform and flow halves cannot disagree about it.
  bool stretchUV;
} UVDeformField;

typedef enum {
  UV_DEFORM_PRESET_SIN_WAVE_TRAIL = 0, // trail_deform.fs mode 2's three fields
  UV_DEFORM_PRESET_SCROLL_V,           // one plain pan down a surface
  UV_DEFORM_PRESET_VORTEX,             // swirl + a detuned ripple
  UV_DEFORM_PRESET_COUNT
} UVDeformPreset;

// --- Construction -----------------------------------------------------------
// Clear sets layerCount = 0 and stretchUV = false; it does NOT wipe the layer
// array, so anything past layerCount is stale by design (nothing reads it).
void UVDeform_Clear(UVDeformField *f);
bool UVDeform_AddLayer(UVDeformField *f, UVDeformLayer layer); // false if full
UVDeformField UVDeform_CreatePreset(UVDeformPreset preset);

// Applies a per-spawn base phase to every SINE layer as `base * layer.param`,
// so a preset's detuned phase relationships survive being re-phased. A field
// whose layers all shared one phase would have its waves crest together and
// read as one thick band instead of several crossing ones.
void UVDeform_SetPhase(UVDeformField *f, float basePhase);

// --- CPU mirror of the GLSL -------------------------------------------------
// These exist so the maths can be asserted headlessly (core/tests/uv_deform_test.c)
// without a GPU. They are transliterations of core/uv/shaders/uv_deform.glsl;
// if you change one, change both, and the test's source mirror will tell you
// when you forgot.
float UVDeform_Envelope(UVEnvelopeKind kind, float c, float start, float end);
// `turns` is the argument in WHOLE TURNS, not radians. Folding is exact here:
// sin(2pi*(n+x)) == sin(2pi*x), so dropping whole turns cannot change the
// result — it only stops the argument outgrowing float32's ability to resolve
// a fraction of a cycle. Taking turns rather than radians also leaves the
// multiply grouping to the caller, which is what lets an existing shader
// migrate onto this function bit-identically.
float UVDeform_SinePhase(float turns, float phase, float amp);
float UVDeform_Sine(float drive, float t, float freq, float speed, float phase,
                    float amp);
// Folds an angle in RADIANS to [0, TAU). For shaders written in radians rather
// than turns, which is most of them: re-expressing one in turns means
// re-tuning every frequency uniform feeding it. Costs about one ULP at small
// angles and buys back far more at large ones.
//
// It does NOT fix frame-to-frame stutter: folding a product already computed
// at full magnitude cannot recover bits that product has lost, and the speed
// cancels out of step-vs-quantum. Fix that at the origin instead — see
// SurfaceFlow_PackGPU, which folds every pan on the C side before upload.
//
// Exact enough for sin/cos and nothing else — never fold an angle feeding an
// aperiodic consumer such as a value-noise domain, which jumps once per cycle.
float UVDeform_FoldAngle(float radians);
float UVDeform_NoiseFlow(float sA, float sB, float amp);

// One layer's contribution as a uv offset. `noisePair` supplies the two raw
// [0,1] samples a UV_DEFORM_NOISE_FLOW layer needs; pass (0.5f, 0.5f) when the
// field has none, which contributes exactly 0. UV_DEFORM_FLOWMAP evaluates to
// zero on the CPU — it needs a texture and exists only on the GPU path, the
// same way FORCE_VECTOR_TEXTURE does in core/force_field.c.
Vector2 UVDeform_EvaluateLayer(const UVDeformLayer *L, Vector2 uv, Vector2 mat,
                               float t, Vector2 noisePair);
Vector2 UVDeform_Evaluate(const UVDeformField *f, Vector2 uv, Vector2 mat,
                          float t, Vector2 noisePair);

// --- GPU --------------------------------------------------------------------
// Packs into UV_DEFORM_PACK_FLOATS floats = UV_DEFORM_MAX_LAYERS * 3 vec4,
// mirrored by core/uv/shaders/uv_field.glsl:
//   [i*12 +  0..3] = (kind, driveAxis, outAxis, amplitude)
//   [i*12 +  4..7] = (frequency, speed, phase, param)
//   [i*12 + 8..11] = (envKind, envAxis, envStart, envEnd)
// and meta = (layerCount, stretchUV, 0, 0).
//
// Zeroes the whole buffer first, then COMPACTS: a layer with zero amplitude is
// skipped, so GPU layer indices are not CPU layer indices. `outMeta` receives
// the packed count, which is what the shader loops to.
void UVDeform_PackGPU(const UVDeformField *f, float *outFloats /*[PACK_FLOATS]*/,
                      float *outMeta /*[4]*/);

// Location cache. -1 means the shader does not declare that uniform, and every
// write is guarded — a shader including only uv_deform.glsl (the pure
// functions) legitimately has neither.
typedef struct {
  int fieldLoc;
  int metaLoc;
} UVDeformLocs;

UVDeformLocs UVDeform_CacheLocations(Shader shader);

// Sets the packed uniforms on `shader`. MUST be called after
// BeginShaderMode(shader) for that same shader: under rlvk the shader argument
// only supplies the location, while the destination is whichever shader is
// currently bound.
void UVDeform_Apply(const UVDeformField *f, Shader shader,
                    const UVDeformLocs *locs);

#endif // UV_DEFORM_H
