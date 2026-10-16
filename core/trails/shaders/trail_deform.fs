#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/vfx_contrast.glsl"
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/uv/shaders/uv_deform.glsl"
#include "core/uv/shaders/surface_flow.glsl"

// u_uvField/u_uvMeta ONLY — declared directly, NOT pulled in from the
// uv_field GLSL module. That module's own job is bundling these
// declarations with helper functions (UVDeform_ApplyField,
// SurfaceFlow_FieldSample) this shader does not need — w0/w1/w2 below call
// UVDeform_LayerOffset directly (already available from the uv_deform
// module, pulled in two lines up) with manually-sliced u_uvField[i] ranges,
// so the ONLY thing missing was the uniform declaration itself.
//
// LANDMINE, hit twice on a real build, 05/08/2026 — read this before
// changing anything above this comment, and see core/shader_preprocessor.c
// for the mechanism: that loader finds its target by bare text search, with
// NO awareness of GLSL comments and NO per-path dedup (unlike a real C
// preprocessor). The uv_field module re-pulls in the uv_deform and
// surface_flow modules itself, and this file already pulls both in
// directly two lines up, so naming that third module by writing the
// directive keyword immediately followed by its quoted path — ANYWHERE in
// this file, even inside a prose comment explaining not to — makes the
// loader pull it in for real, duplicating the uv_deform/surface_flow
// bodies in the flattened output and breaking compilation two different
// ways depending on exactly how the duplication lands. This paragraph
// itself deliberately never spells that keyword-plus-quoted-path pattern
// for that reason — describe the mechanism in prose, never write the
// literal pattern, not even as a "don't do this" example.
#define UV_DEFORM_MAX_LAYERS 4
uniform vec4 u_uvField[UV_DEFORM_MAX_LAYERS * 3];
uniform vec4 u_uvMeta;

// ── TRAIL DEFORM FRAGMENT — packed 4-channel wisp material ──────────────────
// ONE RGBA texture carries four roles (the "packed" convention), so one bind
// and one asset replace the sheet + flow map + mask trio:
//   R = coarse wisp shape   G = fine wisp detail
//   B = dissolve noise      A = high-frequency turbulence
//
// Two samples of the SAME texture (one bind, one asset, cache-friendly):
// R+B ride the coarse pan, G+A ride the fine pan, so the coarse and fine wisp
// layers scroll at different speeds — the internal motion that makes the
// strip read as thick matter, not a flat sticker. A-channel turbulence
// modulates the coarse<->fine mix (self-referential ripple of the wisp edge,
// no third sample needed), and B pans the dissolve edge so the tail erodes
// organically as the noise field moves through it.
//
// ── THE TWO PASSES ARE NOT THE SAME FORMULA (u_renderPass) ──────────────────
// This shader used to emit one colour for both passes, on the belief that
// BLEND_ALPHA and BLEND_ADDITIVE both consume `src.rgb * src.a`. Only the
// additive one does. raylib's BLEND_ALPHA is glBlendFunc(SRC_ALPHA,
// ONE_MINUS_SRC_ALPHA) — NOT premultiplied — so the hardware multiplies by
// alpha itself. Emitting an already-intensity-scaled RGB there gets it scaled a
// SECOND time, and the body pass collapses to ~intensity², contributing almost
// no coverage. The trail then exists only in the additive pass, and additive
// over a bright destination cannot add contrast: the whole effect washes out
// against a bright sky or a lit floor. That is the bug, and it is why a VFX can
// look right over the night arena and vanish over daylight.
//
//   u_renderPass 0 = BODY (BLEND_ALPHA): unpremultiplied colour + a COVERAGE
//                    alpha. This pass is what keeps the element hue readable
//                    over a bright destination — it occludes.
//   u_renderPass 1 = EMISSION (BLEND_ADDITIVE): colour * intensity * HDR gain.
//
// Trails run both semantic passes. BODY carries coloured coverage; EMISSION
// carries HDR radiance. Keeping gain out of BODY prevents an energy trail from
// becoming an opaque red material strip and lets smoke retain its soft edge.

// ── MODE 2 — SIN-WAVE STRAND TRAIL ──────────────────────────────────────────
// Implements the RzFX "Sin Wave Trail" breakdown (ryanzengvfx.blogspot.com,
// 2019-04). Two structural facts drive the whole thing:
//
// 1. THE WAVE IS IN UV SPACE, NOT IN THE VERTICES. The strip stays a wide flat
//    quad; the trail snakes inside it. Geometry cannot fold through itself, and
//    the waveform is anchored in METRES of laid path (u_pathArc) so a growing
//    trail or an accelerating head does not rubber-band it — only u_time moves
//    the crests.
//
// 2. THE TRAIL IS THREE OVERLAPPING STRAND BUNDLES, NOT ONE BAND. The article's
//    Sin01/02/03 blocks are three independent wave fields, each sampling a
//    trail-pattern channel at its own offset. That overlap IS the braided,
//    frayed look. A single analytic band — however well eroded by noise — can
//    only ever be one smooth-edged ribbon, which is precisely the "biên và đuôi
//    liền mạch" failure this mode replaced. There is therefore NO band function
//    here: the silhouette comes from the sheet's own strand density.
//
// Everything that frays is multiplied by `ramp` (the along-trail coordinate),
// per the article's "multiply U coordinate to keep shape in the start position
// and more distortion to the end": the head stays a tight coherent ribbon and
// the tail is where the strands fan apart and dissolve.

uniform sampler2D texture0;
uniform float u_matMode;        // < 1.5 = passthrough, >= 1.5 = strand material
uniform float u_wispMix;        // coarse(R) -> fine(G)
uniform float u_dissolve;       // B-channel dissolve threshold
uniform float u_dissolveSoft;   // dissolve edge softness
                                // threshold, strongest at the band edges —
                                // the silhouette frays ("rách") like smoke
uniform vec2  u_tiling;         // x = tiles along path, y = tiles across width
uniform vec4  u_panSpeed;       // x = coarse pan, y = fine pan (UV units/sec)
uniform float u_tailFadeA;      // segment fade start (0 = head, 1 = tail)
uniform float u_tailFadeB;      // segment fade end — tail dissolves to zero here
                                // (tailFadeA >= tailFadeB disables the ramp)

// Mode 2 only. u_wavePhase and u_waveEnv are DECLARED IN BOTH STAGES on
// purpose: one program, one uniform location, so the C layer keeps setting
// them once and the band's phase/head-fade stay in lockstep with the vertex
// stage's (which mode 2 leaves in passthrough anyway).
//
// u_sinWave is GONE, 05/08/2026 — the three wave fields' amplitude/frequency
// /speed/spread now live in u_uvField/u_uvMeta (uv_field.glsl, included
// above), pushed as a 3-layer core/uv UVDeformField from
// trail_system.c's ApplyDeformUniforms. See the w0/w1/w2 block below.
uniform vec4  u_bandShape;  // x = strand bundle half-width (0..1 of half-width)
                            // y = quad edge-mask softness
                            // z = HDR gain (> 1 feeds bloom)
                            // w = strand gain (contrast; higher = more separated)
uniform vec4  u_strandFlow; // x = B-channel flow distortion strength
                            // y = weight of the third (widest) strand bundle
                            // z = 0 tile the sheet by metres of path,
                            //     1 STRETCH it once over the whole trail
                            // w = unused
uniform vec4  u_tailShape;  // how the tail comes apart:
                            // x = stagger — how far the three bundles' ends are
                            //     spread apart in segment space
                            // y = extra dissolve threshold over the last stretch
                            // z = bundle narrowing at the end (1 = no narrowing)
                            // w = unused
uniform vec2  u_pathArc;    // x = metres at the HEAD node, y = laid length (m)
uniform vec3  u_colHot;     // HDR core colour (hot centre of a strand)
uniform vec3  u_colTail;    // colour at the TAIL; vColor.rgb is the head
uniform float u_wavePhase;  // per-spawn random phase — desyncs simultaneous casts
uniform vec2  u_waveEnv;    // x = head fade fraction (band welded to the emitter)
uniform float u_renderPass; // 0 = BODY (alpha), 1 = EMISSION additive,
                            // 2 = EMISSION premultiplied
uniform float u_bodyOpacity;// coverage scale for the body pass, 0..1
uniform float u_tonemapSafe;// 1 only for migrated MAGIC trail radiance
uniform vec4  u_contrastParams; // enabled, edgeSharpness, coreSize, coreIntensity
// WHERE THE HOT CORE IS. Both were literals in this file until 27/08/2026, and
// the pair is what decides whether a hot core reads as a FILAMENT or a slab:
//   x = geometric centreline half-width, in the across-strip units the bundle
//       distances d0/d1/d2 are measured in. The inner/outer ratio of the
//       centreline's falloff is preserved (the old pair was 0.02 : 0.18), so
//       passing 0.18 reproduces the old constants exactly.
//   y = the strand DENSITY above which the body itself burns toward u_colHot.
//       The old 0.45 turns any reasonably dense fragment hot, which on a
//       gap-filling `strandGain` means the whole packed body goes hot at once —
//       a slab. Raising it confines the hot colour to the true peaks.
// trail_system.c resolves 0 to the old literals before pushing, so this shader
// never carries the defaults itself.
uniform vec2  u_coreShape;

// ── WebGL Style & 4-Color Ramp Extensions ───────────────────────────────────
uniform float u_style;        // 0 = standard strand, 1..15 = specialized WebGL styles
uniform vec3  u_colRamp[4];   // 4-stop gradient ramp (C0, C1, C2, C3)
uniform vec4  u_p0;
uniform vec4  u_p1;
uniform vec4  u_p2;
uniform vec2  u_uvScale;
uniform float u_seed;
uniform float u_maxDist;
uniform float u_add;
uniform float u_alpha;
uniform vec4  u_wave;

uniform int u_nodeArc;
in float vPathMetres;
in vec2 vSegUV;
in vec4 vColor;

const float TAU = 6.2831853;

float Hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float Hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float VNoise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash21(i), Hash21(i + vec2(1.0, 0.0)), f.x),
               mix(Hash21(i + vec2(0.0, 1.0)), Hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float Fbm2D(vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * VNoise2D(p);
        p = p * 2.03 + vec2(17.1, 9.3);
        a *= 0.5;
    }
    return s;
}

float FlowFbm2D(vec2 uv, vec2 dir, float strength, float t)
{
    vec2 fv = (vec2(VNoise2D(uv * 1.3 + 7.7), VNoise2D(uv * 1.3 + 1.9)) - 0.5) * 2.0 * strength + dir;
    float p0 = fract(t);
    float p1 = fract(t + 0.5);
    float w = abs(1.0 - 2.0 * p0);
    return mix(Fbm2D(uv - fv * p0 * 0.7), Fbm2D(uv - fv * p1 * 0.7 + 0.37), w);
}

vec3 Ramp(float t)
{
    t = clamp(t, 0.0, 1.0);
    if (t < 0.3333) return mix(u_colRamp[0], u_colRamp[1], t * 3.0);
    if (t < 0.6666) return mix(u_colRamp[1], u_colRamp[2], (t - 0.3333) * 3.0);
    return mix(u_colRamp[2], u_colRamp[3], (t - 0.6666) * 3.0);
}

vec3 SampleRamp(float t)
{
    return Ramp(t);
}

vec3 Voronoi2D(vec2 x)
{
    vec2 n = floor(x);
    vec2 f = fract(x);
    float d1 = 8.0, d2 = 8.0, id = 0.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 of = vec2(Hash21(n + g), Hash21(n + g + 37.7));
            vec2 r = g + of - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                id = Hash21(n + g + 11.1);
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return vec3(sqrt(d1), sqrt(d2) - sqrt(d1), id);
}

float SegDist(vec2 p, vec2 a, vec2 b, float w)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return 1.0 - smoothstep(w, w + 0.04, length(pa - ba * h));
}

float Glyph3x3(vec2 p, float id)
{
    float r = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 a = floor(vec2(Hash11(id + fi * 1.7), Hash11(id + fi * 3.1 + 5.0)) * 3.0) / 2.0;
        vec2 b = floor(vec2(Hash11(id + fi * 5.3 + 9.0), Hash11(id + fi * 2.3 + 13.0)) * 3.0) / 2.0;
        r = max(r, SegDist(p, a * 0.7 + 0.15, b * 0.7 + 0.15, 0.045));
    }
    return r;
}

// A ribbon is not a uniform emitter: most of its area is the colour-bearing
// carrier and only the dense crest is HDR excess. Preserve only the fraction
// above the project's 1.25 bloom threshold; sub-threshold material remains byte-identical.
vec3 TrailToneMapSafeRadiance(vec3 radiance)
{
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    float safeWeight = clamp((peak - 1.25) / max(peak, 0.00001), 0.0, 1.0);
    return mix(radiance, VFX_TonemapSafeHDR(radiance), safeWeight);
}

vec4 ResolvePass(vec3 colour, float inten, float vAlpha, float gain)
{
    if (u_renderPass < 0.5)
    {
        if (u_bodyOpacity <= 0.001) discard;
        float bodyMask = VFXContrast_BodyMask(inten, u_contrastParams);
        float coverage = clamp(bodyMask * vAlpha * u_bodyOpacity, 0.0, 1.0);
        return VFX_ResolveBody(colour, 1.0, coverage);
    }
    if (u_renderPass < 1.5)
    {
        return VFX_ResolveEmission(colour, gain, 1.0, inten * vAlpha);
    }
    float cover = clamp(inten * vAlpha, 0.0, 1.0);
    vec3 radiance = VFX_Finite3(colour * max(gain, 0.0) * cover);
    if (u_tonemapSafe > 0.5)
        radiance = TrailToneMapSafeRadiance(radiance);
    return vec4(radiance, cover);
}

vec4 ResolvePassEx(vec3 bodyCol, vec3 emissiveCol, float inten, float vAlpha, float gain, bool isAdditive)
{
    float a = clamp(inten * vAlpha, 0.0, 1.0);
    if (u_renderPass < 0.5)
    {
        if (u_bodyOpacity <= 0.001 || isAdditive) discard;
        float coverage = clamp(a * u_bodyOpacity, 0.0, 1.0);
        return VFX_ResolveBody(bodyCol, 1.0, coverage);
    }
    // Emission pass (or single pass) under BLEND_ALPHA_PREMULTIPLY (GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
    // isAdditive == true: outAlpha = 0.0 ensures pure additive radiance without darkening background.
    // isAdditive == false: outAlpha = a ensures exact premultiplied alpha blending (silk, ink, etc.).
    float outAlpha = isAdditive ? 0.0 : a;
    vec3 radiance = VFX_Finite3(emissiveCol * max(gain, 0.0) * a);
    if (u_tonemapSafe > 0.5)
        radiance = TrailToneMapSafeRadiance(radiance);
    return vec4(radiance, outAlpha);
}

void main()
{
    if (u_matMode < 1.5)
    {
        finalColor = texture(texture0, vSegUV) * vColor;
        return;
    }

    float along = vSegUV.y;                   // 0 = head .. 1 = tail
    float across = (vSegUV.x - 0.5) * 2.0;    // -1 .. 1 across the width
    float metres = u_nodeArc!=0?vPathMetres:u_pathArc.x - along * u_pathArc.y;
    float e = 1.0 - abs(across);

    // ── PROCEDURAL PRESET STYLES (WebGL Trail Lab Pipeline) ─────────────────
    if (u_style > 0.5)
    {
        float v = vSegUV.x;                   // 0..1 across width (vUV.y in WebGL)
        float e = 1.0 - abs(2.0 * v - 1.0);   // edge falloff (1 - abs(2v - 1))
        float a = vSegUV.y;                   // age 0 (head) .. 1 (tail) (vAge in WebGL)
        float vDist = a * max(u_pathArc.y, 1.0); // distance in metres from head
        if (u_maxDist > 0.0 && vDist > u_maxDist) discard;

        float vS = u_pathArc.x - vDist;       // worldArcLen (stationary along world path)
        float u = -vS * (u_uvScale.x > 0.001 ? u_uvScale.x : 1.0);
        float vShade = sin(vS * u_wave.y + u_time * u_wave.z + u_seed * 6.2831853);
        float vSpeed = 0.85;

        vec4 c = vec4(0.0);
        int st = int(u_style + 0.5);

        if (st == 1) // Kiếm Khí (Blade / Sword Qi)
        {
            float vb = (v - 0.5) / 1.25 + 0.5;
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, vb * u_p0.z));
            float edge = pow(clamp(vb, 0.0, 1.0), u_p0.w);
            float f = edge * 0.8 + n * 0.45;
            float thr = a * 1.2 - 0.08;
            float m = smoothstep(thr - 0.05, thr + 0.22, f);
            float core = smoothstep(thr + 0.15, thr + 0.7, f);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            float tip = pow(clamp(vb, 0.0, 1.0), 10.0) * (1.0 - a);
            float outside = exp(-max(vb - 1.0, 0.0) * 9.0) * smoothstep(1.2, 0.98, vb);
            float inner = smoothstep(-0.02, 0.35, vb);
            c = vec4(Ramp(core * 0.7 + tip * 0.5 + 0.08) * (1.0 + tip * 1.2), m * fade * inner * outside);
        }
        else if (st == 2) // Phi Kiếm (Flying Sword Comet / Streak)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z));
            float core = pow(e, u_p1.x);
            float glow = pow(e, u_p1.y);
            float pulse = pow(0.5 + 0.5 * sin(u * u_p0.w - u_time * u_p2.x), 3.0);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.z);
            float hh = exp(-vDist * 8.0);
            float body = glow * (0.35 + 0.9 * n) + pulse * glow * 0.5;
            c = vec4(Ramp(clamp(body * 0.7 + core * 0.8 + hh, 0.0, 1.0)) * (1.0 + core * 1.5), (body * 0.8 + core + hh * 0.6) * fade);
        }
        else if (st == 3) // Tiên Đới (Celestial Silk)
        {
            float sh = 0.65 + 0.35 * vShade;
            float lines = 0.5 + 0.5 * sin(v * u_p0.x + Fbm2D(vec2(u * 0.8, v * 2.0)) * 3.0);
            float band = smoothstep(0.7, 1.0, sin(u * u_p0.y - u_time * u_p0.z));
            float border = (1.0 - smoothstep(0.0, 0.035, abs(v - 0.13))) + (1.0 - smoothstep(0.0, 0.035, abs(v - 0.87)));
            vec3 base = Ramp(v * 0.9 + Fbm2D(vec2(u * 1.2, v * 3.0)) * 0.25);
            vec3 col = base * sh * (0.85 + 0.15 * lines) + band * 0.3 * u_colRamp[3] + border * vec3(1.0, 0.85, 0.5) * 0.7;

            // Soft pearl sheen at the head without bleaching silk hue
            float headHot = exp(-vDist * 6.5) * pow(e, 0.65);
            col += vec3(1.0, 0.9, 0.8) * headHot * 0.75;

            c = vec4(col, smoothstep(0.0, 0.1, e) * pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * 0.95);
        }
        else if (st == 4) // Linh Toàn (Dash / Wood Spiral Qi)
        {
            float fr = fract(u * u_p0.x - u_time * u_p0.y);
            float dash = smoothstep(0.15, 0.35, fr) * (1.0 - smoothstep(0.65, 0.85, fr));
            float core = pow(e, u_p1.x);
            float glow = pow(e, 1.5);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.1, vDist);
            c = vec4(Ramp(0.3 + 0.7 * core) * (1.0 + core), (glow * 0.6 * (0.4 + 0.6 * dash) + core * 0.9) * fade);
        }
        else if (st == 5) // Lôi Điện (Lightning Strike)
        {
            float flick = 0.55 + 0.45 * Hash21(vec2(floor(u_time * u_p0.x), u_seed));
            float core = pow(e, u_p0.y);
            float glow = pow(e, u_p0.z);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            float tw = 0.6 + 0.4 * sin(u * 30.0 - u_time * 60.0);
            c = vec4(mix(Ramp(0.5), vec3(1.0), core) * (1.0 + 2.0 * core), (glow * 0.55 * tw + core) * fade * flick);
        }
        else if (st == 6) // Hỏa Diễm (Rising Fire Jet)
        {
            vec2 q = vec2(v * u_p0.z, u);
            float f = FlowFbm2D(q, vec2(0.0, u_p0.y), u_p0.w, u_time * u_p0.x);
            float heat = e * (1.0 - a * u_p1.x) + (f - 0.5) * u_p1.y + exp(-vDist * 5.0) * 0.5;
            float m = smoothstep(0.12, 0.4, heat);
            c = vec4(Ramp(heat * 1.3), m * (1.0 - a * a) * 1.2);
        }
        else if (st == 7) // Long Khí (Dragon Qi Scales / Tube)
        {
            float fres = pow(1.0 - e, u_p1.x);
            vec2 g = vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z);
            g.x += mod(floor(g.y), 2.0) * 0.5;
            float dd = length(fract(g) - 0.5);
            float scale = smoothstep(0.62, 0.28, dd);
            float edgeL = smoothstep(0.28, 0.5, dd);
            float lam = 0.4 + 0.6 * max(dot(vec3(0.0, 1.0, 0.0), normalize(vec3(0.4, 0.8, 0.5))), 0.0);
            vec3 col = Ramp(0.25 + 0.5 * scale + 0.25 * lam) * (0.55 + 0.7 * scale) * lam + fres * Ramp(0.95) * 1.2 - edgeL * 0.15;
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y);
            c = vec4(col + exp(-vDist * u_p1.z) * Ramp(1.0), (0.35 + 0.5 * scale + fres) * fade);
        }
        else if (st == 8) // Mặc Tích (Calligraphy Ink Wash)
        {
            float n = Fbm2D(vec2(u * 0.7, v * 22.0));
            float rag = (VNoise2D(vec2(u * 5.0, v * 9.0)) - 0.5) * 0.35;
            float dry = smoothstep(0.25, 0.6, n * 0.9 + (1.0 - vSpeed) * 0.8);
            float body = smoothstep(0.12, 0.4, e + rag) * dry;
            float bleed = smoothstep(0.0, 0.4, a) * smoothstep(0.0, 0.6, e) * 0.25;
            float fade = 1.0 - smoothstep(0.6, 1.0, a);
            c = vec4(mix(u_colRamp[0], u_colRamp[1], n * 0.7), (body + bleed * (1.0 - body)) * fade);
        }
        else if (st == 9) // Phù Văn (Talisman Runes)
        {
            vec2 g = vec2(vS * u_p0.x - u_time * u_p0.y, clamp((v - 0.12) / 0.76, 0.0, 1.0) * u_p0.z);
            vec2 id = floor(g);
            float gid = Hash21(id + u_seed);
            float gl = Glyph3x3(fract(g), gid * 100.0) * step(0.12, v) * step(v, 0.88);
            float pulse = 0.6 + 0.4 * sin(u_time * 2.0 + gid * 20.0);
            float line = (1.0 - smoothstep(0.0, 0.03, abs(v - 0.06))) + (1.0 - smoothstep(0.0, 0.03, abs(v - 0.94)));
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            c = vec4(Ramp(0.4 + 0.6 * gl) * (1.0 + gl), (gl * pulse + line * 0.8 + 0.12 * e) * fade);
        }
        else if (st == 10) // Hàn Băng (Ice Crystal Shards)
        {
            vec3 vo = Voronoi2D(vec2(u * u_p0.x, v * u_p0.y));
            float facet = 0.55 + 0.45 * vo.z;
            float edgeL = 1.0 - smoothstep(0.0, 0.08, vo.y);
            float spark = pow(Hash11(vo.z * 91.0 + floor(u_time * 4.0 + vo.z * 10.0)), 12.0);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x);
            c = vec4(Ramp(facet * 0.7 + edgeL * 0.3) * (1.0 + edgeL * 0.8 + spark * 4.0), smoothstep(0.0, 0.35, e) * fade * (0.5 + 0.5 * facet));
        }
        else if (st == 11) // Phong Nhận (Wind Blade / Slashes)
        {
            float sp = u_p0.y * (1.0 + 0.6 * Hash11(u_seed * 7.0));
            float s = VNoise2D(vec2(u * u_p0.x - u_time * sp, u_seed * 10.0));
            float streak = smoothstep(0.35, 0.9, s);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * smoothstep(0.0, 0.08, vDist);
            c = vec4(Ramp(0.4 + 0.6 * streak), pow(e, u_p0.z) * (0.45 + 1.1 * streak) * fade);
        }
        else if (st == 12) // Thập Tự Quang (Cross Radiant Beam)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * 3.0));
            float core = pow(e, u_p1.x);
            float glow = pow(e, u_p1.y);
            float pulse = 0.5 + 0.5 * sin(u * u_p0.z - u_time * u_p0.w);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.z);
            c = vec4(Ramp(clamp(glow * 0.5 + core * 0.6 + n * 0.3, 0.0, 1.0)) * (1.0 + core), (glow * (0.4 + 0.6 * n) * 0.8 + core * (0.6 + 0.4 * pulse)) * fade);
        }
        else if (st == 13) // Yên Vụ & Ma Khí (Smoke / Mist / Demonic Contour)
        {
            vec2 q = vec2(v * u_p0.z + u_seed * 3.1, u * u_p0.x);
            float f = FlowFbm2D(q, vec2(0.0, u_p0.y), u_p0.w, u_time * u_p2.x + u_seed);
            float f2 = Fbm2D(vec2(q.x * 2.1 + 3.0, q.y * 2.3 - u_time * u_p0.y * 1.4));
            float dens = f * 0.75 + f2 * 0.35;
            float s = pow(e, u_p1.z) * (0.35 + dens);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.1, vDist);
            if (u_p2.w > 0.5)
            {
                float band = smoothstep(u_p1.x, u_p1.x + 0.1, s) * (1.0 - smoothstep(u_p1.x + 0.1, u_p1.x + 0.35, s));
                c = vec4(Ramp(0.35 + 0.65 * band) * 1.4, band * fade * 1.3);
            }
            else
            {
                float t_col = dens * u_p2.y + (1.0 - e) * u_p2.z + (1.0 - a) * u_p1.w;
                c = vec4(Ramp(t_col), smoothstep(u_p1.x, u_p1.x + 0.45, s) * fade);
            }
        }
        else if (st == 14) // Tinh Hà (Nebula / Star River)
        {
            float n = Fbm2D(vec2(u * u_p0.x - u_time * u_p0.y, v * u_p0.z));
            float n2 = Fbm2D(vec2(u * u_p0.x * 2.1 + 7.0 - u_time * u_p0.y * 1.6, v * u_p0.z * 2.3 + 3.0));
            float body = pow(e, u_p1.x) * (0.4 + 0.9 * n);
            vec2 sg = vec2(vS * u_p0.w, v * 8.0);
            vec2 id = floor(sg);
            vec2 lp = fract(sg) - 0.5;
            float r_star = Hash21(id + u_seed);
            vec2 off = (vec2(Hash21(id + 3.1), Hash21(id + 7.7)) - 0.5) * 0.5;
            float star = step(0.8, r_star) * (1.0 - smoothstep(0.0, 0.14, length(lp - off))) *
                         (0.5 + 0.5 * sin(u_time * 3.0 + r_star * 40.0)) * smoothstep(0.0, 0.5, e);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.y) * smoothstep(0.0, 0.08, vDist);

            float headHot = exp(-vDist * 7.0) * pow(e, 0.4);
            vec3 starCol = vec3(2.5, 2.3, 3.0) * star;
            vec3 col = Ramp(clamp(body * 0.9 + n2 * 0.25, 0.0, 1.0)) * (1.2 + 0.8 * body) + starCol + vec3(1.8, 1.5, 2.2) * headHot * 3.0;

            c = vec4(col, (body * 0.9 + star + headHot * 0.5) * fade);
        }
        else if (st == 15) // Thái Cực (Twin Yin-Yang Helix)
        {
            float fr = fract(u * u_p0.x - u_time * u_p0.y);
            float flow = 0.78 + 0.22 * smoothstep(0.1, 0.5, fr) * (1.0 - smoothstep(0.6, 0.9, fr));
            float core = smoothstep(0.0, 0.7, e);
            float fade = pow(clamp(1.0 - a, 0.0, 1.0), u_p1.x) * smoothstep(0.0, 0.1, vDist);
            c = vec4(mix(u_colRamp[1], u_colRamp[0], core) * flow, pow(e, 0.5) * fade);
        }

        float pAlpha = c.a * u_alpha;
        /* A fixed alpha discard makes the procedural edge binary at subpixel
         * widths. FXAA sees only the final emissive composite and cannot
         * recover coverage lost here. Feather the cutoff over this fragment's
         * screen-space alpha footprint before blending. */
        float alphaFootprint = max(fwidth(pAlpha), 1e-5);
        pAlpha *= smoothstep(0.003 - alphaFootprint,
                             0.003 + alphaFootprint, pAlpha);
        if (pAlpha < 1e-5) discard;

        bool isAdditive = (u_add > 0.5);
        vec3 emissiveCol = c.rgb;
        vec3 bodyCol = clamp(c.rgb, 0.0, 1.0);

        finalColor = ResolvePassEx(bodyCol, emissiveCol, pAlpha, vColor.a, max(u_bandShape.z, 1.0), isAdditive);
        return;
    }

        // Arc-length anchored centreline. `metres` is where THIS fragment sits
        // along the path the emitter actually laid, so a trail that is still
        // growing (or a head that speeds up) slides new strip past a standing
        // waveform instead of stretching the whole wave with it — the exact
        // failure that made vertex-space sines read as a rigid swinging rope.
        // u_pathArc.x is the HEAD's distance and along grows toward the tail,
        // hence the subtraction. The `+ u_time` sign then walks the crests
        // toward the TAIL (constant phase => metres decreasing), which is the
        // direction energy sheds from a moving emitter.
        metres = u_nodeArc!=0?vPathMetres:u_pathArc.x - along * u_pathArc.y;
        // ph (u_wavePhase) is no longer read here — the per-spawn phase is
        // now baked into each packed SINE layer's own `phase` field
        // (trail_system.c's ApplyDeformUniforms: d->phase, d->phase*2.3,
        // d->phase*4.1), same as freq/speed/amplitude. Still declared as a
        // uniform above and still read by the VERTEX stage (mode 2 leaves
        // that in passthrough, but the uniform is shared with the other
        // modes) — just no longer needed as a local here.

        // `ramp` is the article's "multiply by the U coordinate". It gates
        // EVERY source of disorder — wave amplitude, flow distortion, dissolve
        // — so the trail leaves the emitter as one tight coherent ribbon and
        // only comes apart behind it. Without it the head frays too and the
        // effect loses its anchor. The smoothstep is the head weld: excursion
        // is exactly 0 at the emitter.
        //
        // This is UV_ENV_HEAD_WELD in core/uv/shaders/uv_deform.glsl — the
        // shape was general enough to name, so the module names it and this
        // shader is one of its callers rather than its only implementation.
        float ramp = UVDeform_Envelope(UV_ENV_HEAD_WELD, along, 0.0,
                                       max(u_waveEnv.x, 0.001));

        // ── Phase 3: flow distortion from the B channel ──────────────────
        // Two samples at different pan offsets, subtracted from 0.5 so the
        // pair is zero-mean: a biased mean would slide the whole trail
        // sideways instead of warping it. This is what tears the strand
        // bundles apart from each other toward the tail.
        // Every time-driven offset is folded with fract() before it is added
        // (the reference's Step 12). u_time * speed grows without bound, and a
        // float32 loses its low bits long before the effect is retired; the
        // sheet wraps REPEAT and sine has period 2pi, so folding changes
        // nothing visible while it keeps the arithmetic in [0,1].
        float panA = SurfaceFlow_Pan(u_time, u_panSpeed.x);
        float panB = SurfaceFlow_Pan(u_time, u_panSpeed.y);

        vec2 fuv = vec2(vSegUV.x, fract(metres * u_tiling.x * 0.5));
        float b1 = texture(texture0, fuv + vec2(0.0, -panA)).b;
        float b2 = texture(texture0, fuv * 1.37 + vec2(0.29, -panB * 0.7)).b;
        float flow = ((b1 - 0.5) + (b2 - 0.5)) * u_strandFlow.x * ramp;

        // ── Phase 1/2: three independent wave fields (Sin01/02/03) ────────
        // GENERALISED 05/08/2026 onto core/uv's packed UVDeformField
        // (u_uvField/u_uvMeta from uv_field.glsl) — was u_sinWave, hand-
        // detuned per bundle right here. Each bundle is now its own packed
        // SINE layer (trail_system.c's ApplyDeformUniforms builds all three
        // from the SAME numbers this file used to detune by hand: freq/speed
        // /phase/amplitude scaled by `spread` once, on the C side, not per
        // fragment). Keeping them as SEPARATE layers — rather than summing
        // into one centreline — is still the entire point: each carries its
        // own strand bundle, and the bundles cross.
        //
        // `ramp` is no longer computed into `amp` by hand either — it is
        // UVDeform_LayerOffset's OWN envelope term (env=HEAD_WELD, envAxis=1
        // reads mat.y=along), the same UV_ENV_HEAD_WELD this file already
        // used for `ramp` above — so mat.y must carry `along` for the
        // envelope to gate correctly, and mat.x carries `metres` to drive the
        // sine, matching driveAxis=0 packed on the C side.
        //
        // Algebraically identical to the old per-bundle detune, not
        // guaranteed bit-identical — the generic path always multiplies
        // amplitude*envelope in that fixed order, so a detuned layer's
        // product can differ from the old `(amp*ramp)*detune` grouping by up
        // to 1 ULP. Invisible on screen; see trail_system.c's own note at the
        // push site for why that bar is different from trail_volume.fs's.
        vec2 driveMat = vec2(metres, along);
        float w0 = UVDeform_LayerOffset(u_uvField[0], u_uvField[1], u_uvField[2],
                                        vSegUV, driveMat, u_time, vec2(0.5)).x;
        float w1 = UVDeform_LayerOffset(u_uvField[3], u_uvField[4], u_uvField[5],
                                        vSegUV, driveMat, u_time, vec2(0.5)).x;
        float w2 = UVDeform_LayerOffset(u_uvField[6], u_uvField[7], u_uvField[8],
                                        vSegUV, driveMat, u_time, vec2(0.5)).x;

        // ── Sample one strand bundle per wave field ──────────────────────
        // Each bundle is the sheet's strand density mapped across
        // [centre - bundleHW, centre + bundleHW]. The V axis runs in METRES so
        // the filaments stay pinned to the path the emitter laid and read as
        // matter being shed, not as a texture sliding over a surface.
        // ── The along-trail texture coordinate: TILE or STRETCH ───────────
        // STRETCH is the reference's own convention. Its R/G channels are not a
        // repeating material — each is ONE complete smoke streak whose head and
        // tail taper are painted into the sheet — so U maps once across the
        // whole trail. Tiling such a sheet by metres destroys exactly the thing
        // it was authored for: you get a rope of identical segments with no
        // head, no tail and no silhouette, and three sin-offset samples of a
        // rope are just three ropes.
        //
        // TILE is for sheets authored as a repeating filament material (the
        // energy style). There the fold rules matter: never nest them —
        // fract(fract(x)*1.6) != fract(x*1.6) changes the tiling RATE and chops
        // each bundle into mismatched runs.
        // ── Where each bundle ENDS ────────────────────────────────────────
        // Three different points, not one. A tail whose every layer stops on
        // the same line reads as clipped no matter how soft the fade over it —
        // the softness only blurs the cut, it does not remove it. Staggering
        // the ends lets the trail unravel across a stretch: the earliest
        // bundle thins out first and the last one carries a few surviving
        // wisps past it.
        float endMid = clamp(u_tailFadeA, 0.0, 1.0);
        float endLate = clamp(u_tailFadeA + u_tailShape.x, 0.0, 1.0);
        float endEarly = clamp(u_tailFadeA - u_tailShape.x, 0.0, 1.0);
        float dying = smoothstep(endEarly, 1.0, along); // 0 in the body, 1 at the tip

        // Bundles NARROW as they die. A band that only fades keeps its full
        // width to the very end and reads as a rectangle going transparent;
        // real smoke thins to threads before it disappears.
        float bundleHW = max(u_bandShape.x, 0.02) *
                         mix(1.0, clamp(u_tailShape.z, 0.05, 1.0), dying);
        //
        // SurfaceFlow_AlongV (core/uv/shaders/surface_flow.glsl) IS this rule:
        // it takes vBase unfolded and folds it once together with the layer's
        // own scale, which is the non-nesting the comment above demands.
        float vBase = metres * u_tiling.x;
        bool stretch = u_strandFlow.z > 0.5;
        float v0 = SurfaceFlow_AlongV(along, vBase, 1.00, panA, stretch);
        float v1 = SurfaceFlow_AlongV(along, vBase, 1.60, panB, stretch);
        float v2 = SurfaceFlow_AlongV(along, vBase, 0.70, panA * 0.55, stretch);
        float s0 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w0, bundleHW), v0)).r;
        float s1 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w1, bundleHW * 0.72), v1)).g;
        float s2 = texture(texture0, vec2(SurfaceFlow_AcrossU(across, w2, bundleHW * 1.35), v2)).r;

        // A wrap guard that stays flat across 80% of the bundle turns a white
        // fallback texel (or any broad R/G patch) into three solid ribbons.
        // Use a real cross-section instead: full only on the centreline, then
        // taper continuously to zero. The authored sheet still supplies all
        // fine hairs and gaps; this envelope guarantees that it can never
        // inflate into a flat rectangular band.
        float d0 = abs(across - w0) / bundleHW;
        float d1 = abs(across - w1) / (bundleHW * 0.72);
        float d2 = abs(across - w2) / (bundleHW * 1.35);
        float env0 = 1.0 - smoothstep(0.04, 1.0, d0);
        float env1 = 1.0 - smoothstep(0.04, 1.0, d1);
        float env2 = 1.0 - smoothstep(0.04, 1.0, d2);
        s0 *= env0;
        s1 *= env1;
        s2 *= env2;

        // Each bundle's own end. Applied per bundle, BEFORE the max: after it,
        // one shared cut would just clip the combined result again.
        float end0 = 1.0 - smoothstep(endMid, 1.0, along);
        float end1 = 1.0 - smoothstep(endLate, 1.0, along);
        float end2 = 1.0 - smoothstep(endEarly, 1.0, along);
        s0 *= end0;
        s1 *= end1;
        s2 *= end2;

        // The hot core is a structural part of an energy strand, not an
        // accidental threshold in the source texture. Do NOT multiply this
        // by s0/s1/s2: the authored R/G hairs need not cross the exact bundle
        // centre, and doing so made hotSignal identically zero while the red
        // support remained visible. Texture owns fine gaps; geometry guarantees
        // one narrow hot centreline for each live bundle.
        // 0.1111 = the old inner/outer ratio (0.02 / 0.18) kept as a ratio, so
        // narrowing the core narrows its falloff with it instead of turning a
        // thin core into a hard-edged wire.
        float coreOut = max(u_coreShape.x, 0.004);
        float coreIn  = coreOut * 0.1111;
        float core0 = (1.0 - smoothstep(coreIn, coreOut, d0)) * end0;
        float core1 = (1.0 - smoothstep(coreIn, coreOut, d1)) * end1 *
                      clamp(u_wispMix, 0.0, 1.0);
        float core2 = (1.0 - smoothstep(coreIn, coreOut, d2)) * end2 *
                      clamp(u_strandFlow.y, 0.0, 1.0);
        float centreCore = max(max(core0, core1), core2);

        // MAX, not sum: overlapping hairs must stay hairs. Summing fills the
        // gaps between them back in and the three bundles merge into exactly
        // the one smooth band this mode exists to avoid.
        float strand = max(max(s0, s1 * clamp(u_wispMix, 0.0, 1.0)),
                           s2 * clamp(u_strandFlow.y, 0.0, 1.0));
        strand = pow(clamp(strand, 0.0, 1.0), max(u_bandShape.w, 0.05));

        // ── Phase 4: edge mask ────────────────────────────────────────────
        // "Clean the shape animation over the edge" — nothing survives past
        // the quad boundary, whatever the waves did.
        float edgeMask = 1.0 - smoothstep(1.0 - max(u_bandShape.y, 0.01), 1.0, abs(across));

        // ── Phase 5: dissolve (A channel), biting hardest at the tail ─────
        // The threshold climbs with `ramp`, so the head is untouched and the
        // tail erodes into separate wisps instead of fading out as a whole.
        // NOTE the `false`: the dissolve channel ALWAYS tiles by metres, even
        // for a stretch-authored sheet whose R/G carry one complete streak.
        // That predates this module and is preserved exactly; it is passed
        // explicitly here rather than left implicit so the asymmetry is
        // visible instead of hiding in a missing branch.
        float dis = texture(texture0, vec2(vSegUV.x * 0.83 + 0.17,
                                           SurfaceFlow_AlongV(along, vBase, 0.45,
                                                              panB * 0.35, false))).a;
        // The threshold climbs with `ramp` down the whole trail, then climbs
        // FASTER over the dying stretch. That second term is what turns the
        // end into holes and islands instead of a uniform dimming: the noise
        // field eats through the thin parts first and leaves the dense ones
        // behind for a moment.
        float thr = u_dissolve * ramp + u_tailShape.y * dying;
        float dissolveMask2 = smoothstep(thr, thr + max(u_dissolveSoft, 0.01), dis);

        float tailBand = (u_tailFadeA >= u_tailFadeB)
                             ? 1.0
                             : 1.0 - smoothstep(u_tailFadeA, u_tailFadeB, along);

        float inten = strand * edgeMask * dissolveMask2 * tailBand;
        float hotSignal = centreCore * edgeMask * dissolveMask2 * tailBand;
        // The structural core is deliberately independent of the R/G sheet.
        // Its liveness test must be independent too: discarding on `inten`
        // alone silently killed the yellow core wherever the source texture
        // happened to be dark, leaving only the broad red support visible.
        if (max(inten, hotSignal) < 0.004)
            discard;

        // ── Phase 6: colour ───────────────────────────────────────────────
        // TWO ramps, and they answer different questions. The reference has
        // only the first:
        //   * ALONG the trail (u): head colour -> tail colour. This is what
        //     makes a trail read as cooling, or as smoke thinning out, and
        //     leaving it out is why an earlier pass looked like one flat hue
        //     smeared down the whole ribbon.
        //   * ACROSS a strand (intensity): the dense middle of a filament
        //     burns toward u_colHot. Without it a coloured strand is a neon
        //     stripe rather than something hot.
        vec3 lengthCol = mix(vColor.rgb, u_colTail, along);
        if (dot(u_colRamp[1], u_colRamp[1]) + dot(u_colRamp[2], u_colRamp[2]) > 0.0001) {
            lengthCol = SampleRamp(along);
        }

        float densGate = clamp(u_coreShape.y, 0.05, 0.98);
        float hotMix = max(smoothstep(densGate, 1.0, inten),
                           smoothstep(0.08, 0.45, hotSignal));

        float coreTrans = smoothstep(0.60, 1.0, hotMix);
        vec3 elemCol = mix(lengthCol * (0.8 + inten * 0.4), vec3(1.0, 1.0, 1.0) * 3.2, coreTrans);

        float isFire = step(lengthCol.b, lengthCol.r * 0.65) * step(0.35, lengthCol.r);
        float flameTemp = clamp(mix(inten * 0.55, 1.0, hotSignal * hotSignal) * (1.0 - along * 0.30), 0.0, 1.0);
        vec3 fireCol = calcBlackbodyNormalized(flameTemp);

        vec3 hot = (u_renderPass < 0.5)
                       ? mix(lengthCol, u_colHot, hotSignal * 0.65)
                       : mix(elemCol, fireCol, isFire);

        // Feed the structural centreline into radiance/coverage too.
        float passIntensity = (u_renderPass < 0.5)
                                  ? max(inten, hotSignal * 0.65)
                                  : max(inten, hotSignal);
        finalColor = ResolvePass(hot, passIntensity, vColor.a, u_bandShape.z);
        return;
}
