#ifndef WUXING_SURFACE_FLOW_GLSL
#define WUXING_SURFACE_FLOW_GLSL

#include "core/uv/shaders/flow_map.glsl"

// ============================================================
// WUXING — Surface flow: layered sampling over a deformed coordinate
//
// The sampling half of `mesh + UVDeformField + SurfaceFlow = effect`.
// core/uv/shaders/uv_deform.glsl warps the coordinate; this file decides
// where each texture layer reads and how the layers combine. Warp first,
// then sample.
//
// PURE FUNCTIONS ONLY — no uniforms of its own (see uv_deform.glsl's header
// for why). The Valve two-phase sampler it builds on, FlowMap_SampleTwoPhase,
// comes from flow_map.glsl above.
//
// Provides:
//   SurfaceFlow_Pan()      — a folded pan offset
//   SurfaceFlow_AlongV()   — the TILE-or-STRETCH coordinate rule
//   SurfaceFlow_LayerUV()  — one packed layer -> its sampling uv
//   SurfaceFlow_Blend()    — multiply / add / max
//   FlowMap_SampleTwoPhase() — re-exported from flow_map.glsl
//
// ── SHAPE OR MATERIAL: DECIDE PER SHEET, AND RECORD IT ──────────────────────
// A sheet with head and tail taper painted into it is ONE COMPLETE SHAPE: it
// must be STRETCHED once across the whole surface. A sheet authored as a
// repeating filament pattern is a MATERIAL: it must be TILED by metres so
// texel density stays constant as the surface grows. Tiling a shape sheet
// gives a rope of identical segments with no head, no tail and no silhouette
// — and three offset samples of a rope are just three ropes. The switch is
// explicit (`stretch`) because no code can infer which kind an asset is; it is
// an authoring fact and belongs next to the asset. See core/docs/LANDMINES.md
// "A trail texture is a SHAPE; a filament sheet is a MATERIAL".
// ============================================================

// Blend ops. MUST match SurfaceFlowBlend in core/uv/surface_flow.h.
#define SURFACE_FLOW_MUL 0
#define SURFACE_FLOW_ADD 1
#define SURFACE_FLOW_MAX 2

// ------------------------------------------------------------------
// PAN — a scroll offset, folded to [0,1).
//
// Exact for a REPEAT-wrapped sampler and for a sine, because both are
// periodic with the period this folds to. NOT exact for an aperiodic
// consumer: fold the time term of a value-noise domain and the field jumps
// once per second. When in doubt, ask what the coordinate feeds before
// folding it — a scroll into fbm() must stay unfolded.
// ------------------------------------------------------------------
float SurfaceFlow_Pan(float t, float speed)
{
    return fract(t * speed);
}

// ------------------------------------------------------------------
// THE ALONG-SURFACE COORDINATE — tile by a material measure, or stretch once.
//
//   stretched : the normalised 0..1 coordinate to use in STRETCH mode.
//   base      : the material measure (metres of laid path, arc length...)
//               already multiplied by the surface's tiling.
//   scale     : this layer's own tiling multiplier, so layers at different
//               rates read different parts of the sheet.
//   pan       : a scroll offset, normally from SurfaceFlow_Pan().
//
// `base` arrives UNFOLDED and is folded here exactly once, together with this
// layer's own scale. That ordering is the point: fract(fract(base) * scale)
// is not fract(base * scale), and folding before the scale changes the
// effective tiling rate and chops each layer into mismatched runs.
// ------------------------------------------------------------------
float SurfaceFlow_AlongV(float stretched, float base, float scale, float pan,
                         bool stretch)
{
    return stretch ? stretched : fract(base * scale) - pan;
}

// ------------------------------------------------------------------
// ACROSS the surface, remapped into a band of half-width `halfWidth` centred
// on `centre` — normally the wave offset this layer was given by
// uv_deform.glsl, so the texture rides the swung centreline.
//
// The caller still owns the wrap window: a sheet has to REPEAT to tile along
// the surface, so a band whose centre has swung past this fragment will smear
// its content back in from the opposite edge unless it is masked out.
// ------------------------------------------------------------------
float SurfaceFlow_AcrossU(float across, float centre, float halfWidth)
{
    return 0.5 + (across - centre) / (2.0 * halfWidth);
}

// ------------------------------------------------------------------
// ONE PACKED LAYER -> its sampling uv.
//
//   tilePan = (tilingU, tilingV, panU, panV), pan already folded to [0,1)
//
// `mat` is the material coordinate, `uv` the surface's own 0..1 pair. In
// STRETCH mode the uv passes through untiled — stretching means mapping the
// sheet across the surface exactly once, so a tiling multiplier there would
// defeat the mode.
//
// This does NOT fold, and SurfaceFlow_AlongV above does. That is deliberate,
// not an oversight in one of them:
//   * AlongV takes a raw unbounded material measure (metres of laid path) and
//     must fold it, once, together with the layer's own scale.
//   * this one is the packed-field path, whose `mat` is contracted to arrive
//     already bounded — folded on the C side where the modulus is chosen
//     deliberately. Folding again here would buy nothing and cost something
//     real: fract() puts a derivative discontinuity at every seam, and a
//     triplanar consumer sampling three projected planes would get a
//     one-texel mip artefact along each of them.
// ------------------------------------------------------------------
vec2 SurfaceFlow_LayerUV(vec2 uv, vec2 mat, vec4 tilePan, bool stretch)
{
    if (stretch) return uv - tilePan.zw;
    return mat * tilePan.xy - tilePan.zw;
}

// ------------------------------------------------------------------
// COMBINE two layers.
//
// MAX is the one to reach for when the layers are overlapping strands or
// hairs: summing them fills the gaps between them back in, and the gaps ARE
// the effect. ADD is for genuinely additive energy, MUL for masking.
// ------------------------------------------------------------------
vec4 SurfaceFlow_Blend(vec4 acc, vec4 src, int op)
{
    if (op == SURFACE_FLOW_ADD) return acc + src;
    if (op == SURFACE_FLOW_MAX) return max(acc, src);
    return acc * src; // SURFACE_FLOW_MUL
}

#endif
