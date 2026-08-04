#ifndef WUXING_UV_DEFORM_GLSL
#define WUXING_UV_DEFORM_GLSL

// ============================================================
// WUXING — UV-space deformation: W(uv, t)
//
// The coordinate half of `mesh + UVDeformField + SurfaceFlow = effect`.
// This file WARPS a coordinate; core/uv/shaders/surface_flow.glsl SAMPLES
// with the warped result. Warp first, then sample.
//
// PURE FUNCTIONS ONLY — this file declares no uniforms and no samplers, the
// same contract as core/uv/shaders/flow_map.glsl. Everything arrives as an
// argument, so a shader with its own uniform naming (core/trails/shaders/
// trail_deform.fs) can call these without renaming anything, and a shader
// that wants the engine's standard packing includes uv_field.glsl instead.
//
// It also does NOT include noise.glsl. noise.glsl has no include guard, so
// depending on it here would double-define hash2/vnoise/fbm2 for every
// consumer that already includes it. Noise arrives as a value argument, the
// same pattern as displacement.glsl's DisplaceVertex_Noise().
//
// Provides:
//   UVDeform_Envelope()     — the along-surface gate (5 kinds)
//   UVDeform_SinePhase()    — the periodic primitive; exact
//   UVDeform_Sine()         — SinePhase with the drive/time product built in
//   UVDeform_NoiseFlow()    — two zero-mean samples, added
//   UVDeform_Swirl()        — rotate a uv about a centre, falling off with radius
//   UVDeform_LayerOffset()  — one packed layer -> a uv offset
//
// ── THE DRIVE COORDINATE IS NOT THE UV ──────────────────────────────────────
// Every entry point takes the driving coordinate separately from the uv being
// warped. That separation is deliberate and load-bearing: a scroll built on a
// coordinate measured from a MOVING end of the geometry (a trail's tail, the
// first live particle) reads as frozen no matter how fast you scroll it,
// because the geometry slides under the coordinate at exactly the scroll
// speed. Pass a MATERIAL coordinate — a label stamped on the geometry when it
// was created and never revisited, such as metres of emitter path at the
// moment a node was laid. A flat quad may pass its own uv. See
// core/docs/LANDMINES.md "A scroll built on a MOVING origin is the motion".
//
// ── FOLDING WITH fract() IS EXACT FOR A SINE, AND FOR ALMOST NOTHING ELSE ───
// UVDeform_SinePhase folds its argument because sin(2pi*(n + x)) == sin(2pi*x)
// for integer n: dropping whole turns cannot change the result, it only stops
// the argument growing until float32 can no longer resolve a fraction of a
// cycle. The same fold is exact for a REPEAT-wrapped sampler. It is NOT exact
// for anything aperiodic — folding the time term of a value-noise domain makes
// the field JUMP once a second. Do not add a fold to a consumer without first
// checking what the coordinate feeds.
//
// And never nest folds: fract(fract(x) * k) != fract(x * k). Folding a
// coordinate and then scaling it changes the effective tiling RATE of every
// layer that reuses the folded value. Keep one unfolded base and fold each
// consumer's own final product exactly once.
// ============================================================

#define UV_TAU 6.2831853

// Layer kinds. MUST match UVDeformKind in core/uv/uv_deform.h — GLSL cannot
// include a C header, so the two are kept in sync by hand. Append only:
// (float)kind is written straight into the packed p0.x.
#define UV_DEFORM_SINE       0
#define UV_DEFORM_NOISE_FLOW 1
#define UV_DEFORM_FLOWMAP    2
#define UV_DEFORM_SWIRL      3

// Envelope kinds. MUST match UVEnvelopeKind in core/uv/uv_deform.h.
#define UV_ENV_NONE       0
#define UV_ENV_RAMP       1
#define UV_ENV_BELL       2
#define UV_ENV_SMOOTHSTEP 3
#define UV_ENV_HEAD_WELD  4
#define UV_ENV_HEAD_WELD_SQ 5

// ------------------------------------------------------------------
// ENVELOPE — the along-surface gate. Weights the wave amplitude here and the
// texture blend in surface_flow.glsl, which is why both halves live in one
// module: it is the same weight.
//
//   NONE       1 everywhere.
//   RAMP       linear 0 -> 1 across [start, end], clamped outside.
//   BELL       raised cosine over [start, end]: zero VALUE and zero SLOPE at
//              both ends, peak 1 at the midpoint. Use this to bound a feature
//              so it has a beginning and an end — a scrolling texture made of
//              continuous full-length lanes reads as static however fast it
//              moves, because nothing in it ever starts or stops.
//   SMOOTHSTEP the GLSL builtin, 0 -> 1 across [start, end].
//   HEAD_WELD  smoothstep(start, end, c) * c. The excursion is exactly 0 at
//              c = 0 (the weld) and then grows with c, so an effect leaves its
//              emitter as one coherent piece and only comes apart behind it.
//              This is trail_deform.fs mode 2's `ramp`, exactly.
// ------------------------------------------------------------------
float UVDeform_Envelope(int kind, float c, float start, float end)
{
    if (kind == UV_ENV_NONE) return 1.0;
    if (kind == UV_ENV_SMOOTHSTEP) return smoothstep(start, end, c);
    if (kind == UV_ENV_HEAD_WELD) return smoothstep(start, end, c) * c;
    // S(V) = V^p with p = 2 — see UV_ENV_HEAD_WELD_SQ in core/uv/uv_deform.h.
    if (kind == UV_ENV_HEAD_WELD_SQ) return smoothstep(start, end, c) * c * c;

    float span = end - start;
    float k = clamp((c - start) / (abs(span) < 0.000001 ? 0.000001 : span), 0.0, 1.0);
    if (kind == UV_ENV_BELL) return 0.5 - 0.5 * cos(UV_TAU * k);
    return k; // UV_ENV_RAMP
}

// ------------------------------------------------------------------
// SINE — the periodic primitive.
//
// `turns` is the argument in WHOLE TURNS, not radians: the caller supplies
// drive * frequency + t * speed and this folds it. Taking turns rather than
// radians is what makes the fold exact and keeps the multiply grouping in the
// caller's hands, so a shader migrating onto this function gets bit-identical
// output instead of a 1-ULP regrouping.
// ------------------------------------------------------------------
float UVDeform_SinePhase(float turns, float phase, float amp)
{
    return sin(fract(turns) * UV_TAU + phase) * amp;
}

// Convenience form. Only bit-identical to a hand-rolled expression that
// groups the products the same way — use UVDeform_SinePhase directly when
// migrating an existing shader whose grouping differs.
float UVDeform_Sine(float drive, float t, float freq, float speed,
                    float phase, float amp)
{
    return UVDeform_SinePhase(drive * freq + t * speed, phase, amp);
}

// ------------------------------------------------------------------
// FOLD AN ANGLE IN RADIANS to [0, TAU).
//
// Most shaders in this engine were written in radians rather than turns, and
// re-expressing one in turns means re-tuning every frequency uniform feeding
// it — a change to the look, on an effect nobody can eyeball on a machine
// without a GPU. This lets such a shader keep its own units and still stop its
// clock term growing without bound.
//
// The round trip through TAU costs about one ULP at small angles and buys back
// far more than that at large ones: sin() of a huge argument has to reduce it
// modulo TAU anyway, and doing that reduction while the value still fits is
// what keeps the result accurate.
//
// WHAT IT DOES NOT FIX: folding a product that was already computed at full
// magnitude cannot recover bits that product has lost. Frame-to-frame
// resolution is the case that proves it — the phase step is speed*dt and the
// quantum is (t*speed)*2^-23, so the speed cancels out of their ratio and a
// stuttering animation stays stuttering. That has to be fixed at the ORIGIN,
// on the C side, where the modulus can be chosen deliberately (see
// SurfaceFlow_PackGPU, which folds every pan before uploading it). Do not
// reach for this function to fix a stutter.
//
// Exact enough for sin/cos and for nothing else — do NOT fold an angle that
// feeds an aperiodic consumer such as a value-noise domain, which will jump
// once per cycle instead.
// ------------------------------------------------------------------
float UVDeform_FoldAngle(float radians)
{
    return fract(radians / UV_TAU) * UV_TAU;
}

// ------------------------------------------------------------------
// NOISE FLOW — two samples of a field taken at different pan offsets, each
// biased to zero mean before they are added.
//
// The subtraction is the whole point: a biased pair would slide the surface
// bodily sideways instead of warping it. `sA`/`sB` are raw [0,1] samples; the
// caller reads them, so this file needs neither a sampler nor noise.glsl.
// ------------------------------------------------------------------
float UVDeform_NoiseFlow(float sA, float sB, float amp)
{
    return ((sA - 0.5) + (sB - 0.5)) * amp;
}

// ------------------------------------------------------------------
// SWIRL — rotate uv about `centre`, the angle falling off with radius.
// `falloff` > 0 tightens the vortex toward the centre; 0 rotates rigidly.
// ------------------------------------------------------------------
vec2 UVDeform_Swirl(vec2 uv, vec2 centre, float angle, float falloff)
{
    vec2 d = uv - centre;
    float r = length(d);
    float a = angle * (falloff > 0.0 ? exp(-r * falloff) : 1.0);
    float s = sin(a), c = cos(a);
    return centre + vec2(d.x * c - d.y * s, d.x * s + d.y * c) - uv;
}

// ------------------------------------------------------------------
// ONE PACKED LAYER -> a uv offset.
//
//   p0 = (kind, driveAxis, outAxis, amplitude)
//   p1 = (frequency, speed, phase, param)
//   p2 = (envKind, envAxis, envStart, envEnd)
//
// driveAxis / outAxis / envAxis: 0 selects .x, 1 selects .y.
// `mat` is the material coordinate (see the header note); `uv` is only read by
// UV_DEFORM_SWIRL, which warps the coordinate itself rather than one axis of
// it. `noisePair` carries the two samples a UV_DEFORM_NOISE_FLOW layer needs —
// pass vec2(0.5) when the field has no such layer, which contributes exactly 0.
//
// UV_DEFORM_FLOWMAP needs a sampler and is therefore not handled here; see
// UVDeform_LayerOffsetTex in uv_field.glsl.
// ------------------------------------------------------------------
vec2 UVDeform_LayerOffset(vec4 p0, vec4 p1, vec4 p2, vec2 uv, vec2 mat,
                          float t, vec2 noisePair)
{
    int kind = int(p0.x);
    float drive = (int(p0.y) == 0) ? mat.x : mat.y;
    float envC = (int(p2.y) == 0) ? mat.x : mat.y;
    float amp = p0.w * UVDeform_Envelope(int(p2.x), envC, p2.z, p2.w);

    if (kind == UV_DEFORM_SWIRL)
        return UVDeform_Swirl(uv, vec2(p1.z, p1.w), amp, p1.x);

    float w;
    if (kind == UV_DEFORM_NOISE_FLOW)
        w = UVDeform_NoiseFlow(noisePair.x, noisePair.y, amp);
    else
        w = UVDeform_Sine(drive, t, p1.x, p1.y, p1.z, amp);

    return (int(p0.z) == 0) ? vec2(w, 0.0) : vec2(0.0, w);
}

#endif
