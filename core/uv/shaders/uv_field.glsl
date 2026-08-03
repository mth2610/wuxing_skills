#ifndef WUXING_UV_FIELD_GLSL
#define WUXING_UV_FIELD_GLSL

#include "core/uv/shaders/uv_deform.glsl"
#include "core/uv/shaders/surface_flow.glsl"

// ============================================================
// WUXING — The packed uniform blocks for core/uv
//
// uv_deform.glsl and surface_flow.glsl are pure functions with no uniforms, so
// a shader with its own naming can call them without renaming anything. THIS
// file is the other route: it declares the engine's standard packed uniforms
// for BOTH halves, so a consumer binds a whole field and flow in one C call
// (UVFx_Apply in core/uv/uv_fx.h) and reads them with one GLSL call each.
// Include this one OR just the pure files — including this costs you the
// uniform blocks whether or not you fill them.
//
// Provides:
//   UVDeform_ApplyField()    — sum of every layer -> one warped uv
//   UVDeform_ApplyFieldLayer() — one layer alone -> its own warped uv
//   UVDeform_LayerOffsetTex() — the layer dispatcher including UV_DEFORM_FLOWMAP
//   UVDeform_FieldStretch()  — the SHAPE/MATERIAL switch carried in the meta
//   SurfaceFlow_FieldLayerUV() — one flow layer -> its sampling uv
//   SurfaceFlow_FieldSample()  — every flow layer, sampled and blended
//
// ── SUM OR PARALLEL: BOTH, AND THEY ARE NOT INTERCHANGEABLE ─────────────────
// The reference decomposition sums its layers into one displaced coordinate,
// which is what UVDeform_ApplyField does and what a warped flat quad wants.
// But an effect can also run its layers in PARALLEL — each carrying its own
// sample of the sheet, the samples combined afterwards with max. That is what
// makes a trail read as three braided strand bundles rather than one smooth
// band, and summing those three would produce exactly the single band the
// mode exists to avoid. UVDeform_ApplyFieldLayer serves that case.
//
// ── PACKING (must match UVDeform_PackGPU in core/uv/uv_deform.c) ────────────
//   u_uvField[i*3 + 0] = (kind, driveAxis, outAxis, amplitude)
//   u_uvField[i*3 + 1] = (frequency, speed, phase, param)
//   u_uvField[i*3 + 2] = (envKind, envAxis, envStart, envEnd)
//   u_uvMeta           = (layerCount, stretchUV, 0, 0)
//
// vec4 ONLY. Never declare a float[] or vec3[] uniform array: std140 pads
// every array element to 16 bytes, so a tightly packed upload lands element 0
// correctly and misaligns everything after it. Pack the spare components
// instead — which is why the enums ride as floats in .x, the same convention
// as ForceLayerGPU's params0.w in core/force_field.h.
// ============================================================

// ── PACKING, flow half (must match SurfaceFlow_PackGPU in surface_flow.c) ───
//   u_flowLayer[i*2 + 0] = (tilingU, tilingV, panU, panV)
//   u_flowLayer[i*2 + 1] = (blendOp, envKind, envStart, envEnd)
//   u_flowMeta           = (layerCount, stretchUV, envAxis, 0)
//   u_flowTwoPhase       = (speed, strength, enabled, 0)
//
// The pan components arrive ALREADY folded to [0,1) — the fold happens on the
// C side where the modulus is chosen deliberately, rather than here where an
// unbounded clock would already have lost its low bits.

#define UV_DEFORM_MAX_LAYERS 4
#define SURFACE_FLOW_MAX_LAYERS 4

uniform vec4 u_uvField[UV_DEFORM_MAX_LAYERS * 3];
uniform vec4 u_uvMeta;

uniform vec4 u_flowLayer[SURFACE_FLOW_MAX_LAYERS * 2];
uniform vec4 u_flowMeta;
uniform vec4 u_flowTwoPhase;

// SHAPE (stretch the sheet once) vs MATERIAL (tile it by metres). See the
// header of surface_flow.glsl — this is an authoring fact, not an inferable
// one, and it rides in the field so the two halves cannot disagree.
bool UVDeform_FieldStretch()
{
    return u_uvMeta.y > 0.5;
}

// ------------------------------------------------------------------
// The full layer dispatcher, including UV_DEFORM_FLOWMAP.
//
// The sampler is an ARGUMENT, never declared here: a shader that has no flow
// texture must not be made to declare one, and a shader that does declare
// several must bind each explicitly rather than depending on the implicit
// slot raylib gives texture0.
//
// A FLOWMAP layer reads RG at the material coordinate and decodes to [-1,1].
// p1.x tiles that lookup; the envelope and amplitude scale the result.
// ------------------------------------------------------------------
vec2 UVDeform_LayerOffsetTex(sampler2D flowTex, vec4 p0, vec4 p1, vec4 p2,
                             vec2 uv, vec2 mat, float t, vec2 noisePair)
{
    if (int(p0.x) == UV_DEFORM_FLOWMAP)
    {
        float envC = (int(p2.y) == 0) ? mat.x : mat.y;
        float amp = p0.w * UVDeform_Envelope(int(p2.x), envC, p2.z, p2.w);
        vec2 f = texture(flowTex, mat * p1.x).rg * 2.0 - 1.0;
        return f * amp;
    }
    return UVDeform_LayerOffset(p0, p1, p2, uv, mat, t, noisePair);
}

// ------------------------------------------------------------------
// Every layer, summed. `noisePair` is shared by all NOISE_FLOW layers — pass
// vec2(0.5) when the field has none, which contributes exactly 0.
// ------------------------------------------------------------------
vec2 UVDeform_ApplyField(sampler2D flowTex, vec2 uv, vec2 mat, float t,
                         vec2 noisePair)
{
    int n = int(u_uvMeta.x);
    vec2 offset = vec2(0.0);
    for (int i = 0; i < UV_DEFORM_MAX_LAYERS; i++)
    {
        if (i >= n) break;
        offset += UVDeform_LayerOffsetTex(flowTex, u_uvField[i * 3 + 0],
                                          u_uvField[i * 3 + 1],
                                          u_uvField[i * 3 + 2],
                                          uv, mat, t, noisePair);
    }
    return uv + offset;
}

// ------------------------------------------------------------------
// One layer alone. For consumers whose layers run in parallel rather than
// summed — see the header note.
// ------------------------------------------------------------------
vec2 UVDeform_ApplyFieldLayer(sampler2D flowTex, vec2 uv, vec2 mat, float t,
                              vec2 noisePair, int i)
{
    if (i >= int(u_uvMeta.x)) return uv;
    return uv + UVDeform_LayerOffsetTex(flowTex, u_uvField[i * 3 + 0],
                                        u_uvField[i * 3 + 1],
                                        u_uvField[i * 3 + 2],
                                        uv, mat, t, noisePair);
}

// ------------------------------------------------------------------
// FLOW HALF — one layer's sampling uv.
// ------------------------------------------------------------------
vec2 SurfaceFlow_FieldLayerUV(vec2 uv, vec2 mat, int i)
{
    return SurfaceFlow_LayerUV(uv, mat, u_flowLayer[i * 2 + 0],
                               u_flowMeta.y > 0.5);
}

// ------------------------------------------------------------------
// Every flow layer, sampled from `bodyTex` and blended in order.
//
// Layer 0 optionally runs the Valve two-phase sampler (u_flowTwoPhase.z), which
// cross-fades two copies at opposite phases so the texture never stretches
// past half a cycle. A one-layer two-phase flow is exactly a FlowMap.
//
// Every layer is gated by its own envelope, measured along u_flowMeta.z — the
// SAME envelope that weights a wave's amplitude in the deform half.
// ------------------------------------------------------------------
vec4 SurfaceFlow_FieldSample(sampler2D bodyTex, sampler2D flowTex, vec2 uv,
                             vec2 mat, float t)
{
    int n = int(u_flowMeta.x);
    if (n <= 0) return texture(bodyTex, uv);

    float envC = (int(u_flowMeta.z) == 0) ? mat.x : mat.y;
    vec4 acc = vec4(0.0);

    for (int i = 0; i < SURFACE_FLOW_MAX_LAYERS; i++)
    {
        if (i >= n) break;
        vec4 p1 = u_flowLayer[i * 2 + 1];
        vec2 luv = SurfaceFlow_FieldLayerUV(uv, mat, i);

        // The two-phase sampler owns its own tiling, and is handed the layer's
        // UNTILED coordinate: it deliberately reads the flow field at the raw
        // coordinate and the body at the tiled one, which a pre-tiled uv would
        // destroy. Everything else samples the layer's coordinate directly.
        vec4 src;
        if (i == 0 && u_flowTwoPhase.z > 0.5)
            src = FlowMap_SampleTwoPhase(bodyTex, flowTex, uv, t,
                                         u_flowTwoPhase.x, u_flowTwoPhase.y,
                                         u_flowLayer[0].x);
        else
            src = texture(bodyTex, luv);

        src *= UVDeform_Envelope(int(p1.y), envC, p1.z, p1.w);
        acc = (i == 0) ? src : SurfaceFlow_Blend(acc, src, int(p1.x));
    }
    return acc;
}

#endif
