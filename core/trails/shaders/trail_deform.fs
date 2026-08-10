#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/vfx_contrast.glsl"
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
uniform float u_renderPass; // 0 = BODY (alpha, occludes), 1 = EMISSION (additive)
uniform float u_bodyOpacity;// coverage scale for the body pass, 0..1
uniform vec4  u_contrastParams; // enabled, edgeSharpness, coreSize, coreIntensity

in vec2 vSegUV;
in vec4 vColor;

const float TAU = 6.2831853;

// The one place the two passes differ. Every mode routes its final colour
// through here so the split cannot drift apart per mode.
//   colour : the material's own hue, NOT scaled by intensity
//   inten  : 0..1 coverage/energy of this fragment
//   vAlpha : the vertex alpha (width envelope, lifetime, layer alpha)
//   gain   : HDR lift request for EMISSION only
vec4 ResolvePass(vec3 colour, float inten, float vAlpha, float gain)
{
    if (u_renderPass < 0.5)
    {
        // BLEND_ALPHA is not premultiplied: RGB stays straight and the hardware
        // applies coverage once. Producer-side structure shapes the edge; the
        // shared compositor deliberately does not reshape alpha again.
        float bodyMask = VFXContrast_BodyMask(inten, u_contrastParams);
        float coverage = clamp(bodyMask * vAlpha * u_bodyOpacity, 0.0, 1.0);
        return vec4(colour, coverage);
    }
    // Raylib additive blend applies src alpha. Keep intensity in alpha so it is
    // applied exactly once; pre-scaling RGB here would square soft edges.
    return vec4(colour * gain, inten * vAlpha);
}

void main()
{
    // ── PASSTHROUGH ─────────────────────────────────────────────────────────
    // A plain textured strip: the swept presets, whose shape comes from the
    // CLOTH and whose look is painted into the sheet, plus every trail that has
    // no recipe at all.
    //
    // ONE THRESHOLD, not two. There used to be a middle branch here — the
    // PACKED-WISP material (mode 1: two pans of one sheet, R/G cross-faded by an
    // A-channel turbulence term, B-channel dissolve). It was DELETED 10/08/2026:
    // no composer had set `material.mode = 1` since the strand trail replaced
    // it, so it was thirty lines of shader and two uniforms that could not be
    // reached, sitting in the one file every trail's look gets debugged through.
    // Its dissolve and edge-tear ideas survive in the strand path below, which
    // is the material that actually ships.
    if (u_matMode < 1.5)
    {
        finalColor = texture(texture0, vSegUV) * vColor;
        return;
    }

    {
        float along = vSegUV.y;                   // 0 = head .. 1 = tail
        float across = (vSegUV.x - 0.5) * 2.0;    // -1 .. 1 across the width

        // Arc-length anchored centreline. `metres` is where THIS fragment sits
        // along the path the emitter actually laid, so a trail that is still
        // growing (or a head that speeds up) slides new strip past a standing
        // waveform instead of stretching the whole wave with it — the exact
        // failure that made vertex-space sines read as a rigid swinging rope.
        // u_pathArc.x is the HEAD's distance and along grows toward the tail,
        // hence the subtraction. The `+ u_time` sign then walks the crests
        // toward the TAIL (constant phase => metres decreasing), which is the
        // direction energy sheds from a moving emitter.
        float metres = u_pathArc.x - along * u_pathArc.y;
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
        float core0 = (1.0 - smoothstep(0.02, 0.18, d0)) * end0;
        float core1 = (1.0 - smoothstep(0.02, 0.18, d1)) * end1 *
                      clamp(u_wispMix, 0.0, 1.0);
        float core2 = (1.0 - smoothstep(0.02, 0.18, d2)) * end2 *
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
        float hotMix = max(smoothstep(0.45, 1.0, inten),
                           smoothstep(0.08, 0.45, hotSignal));
        vec3 hot = mix(lengthCol, u_colHot, hotMix);
        // Feed the structural centreline into radiance/coverage too. It is
        // narrow, so this restores the hot filament without re-inflating the
        // surrounding support band.
        // BODY carries a narrower, lower-energy coloured core so its hue is
        // readable on bright maps; EMISSION carries the full HDR copy. The
        // hotSignal is geometric and thin, so this cannot refill the red body.
        float passIntensity = (u_renderPass < 0.5)
                                  ? max(inten, hotSignal * 0.65)
                                  : max(inten, hotSignal);
        finalColor = ResolvePass(hot, passIntensity, vColor.a, u_bandShape.z);
        return;
    }

}
