#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── VFX_ComposeShockRing: a leading edge, and the tail behind it ─────────────
//
// ── WHAT THIS USED TO BE, AND WHY NONE OF IT IS HERE ────────────────────────
//
// Everything this shader drew came out of an authored thin-smoke strip
// (VFX_SURFACE_SHOCK_RING_SMOKE): a closed rope of texture, torn open by an
// erosion threshold that climbed over the ring's life. The sheet was removed on
// 26/08/2026 by owner decision, after the redesign that put a real leading edge
// on the ring — with a front to look at, the sheet's smoke read as lint around
// it rather than as the thing itself.
//
// The history is kept because it is expensive and it is all still true of the
// next person who reaches for a sheet here:
//
//   · A SHEET WRAPPED N TIMES AROUND A RING IS MINIFIED N*W/P, and nothing in
//     this engine builds mip chains. Wrapped 4 and 7 times, a 2048-wide strip
//     put 10 to 18 texels on every screen pixel of an 800-pixel circumference,
//     so features that autocorrelate over ~24 texels were drawn two pixels wide
//     — grain, which the coverage ramps then averaged into flat haze. That is
//     the whole of why this effect looked out of focus for as long as it did.
//     (ENGINE_LANDMINES.md carries the general form.)
//
//   · AN ISO-CONTOUR OF A DENSITY FIELD DOUBLE-EDGES. Lighting {dens == thr}
//     is right for a procedural field, but across a rope the density is a single
//     HUMP: any threshold below the peak is crossed twice, once going up and
//     once coming down, so the contour draws an inner rim AND an outer one. The
//     front below is a band on `v`, the canvas COORDINATE, which is monotonic
//     across the section — one crossing by construction.
//
//   · A GENERATIVE FIELD WITH ONE FEATURE PER ANGULAR CELL IS A COMB. Evenly
//     spaced features, by definition; domain warp and per-strand jitter make a
//     prettier comb because none of them touch the period. Do not propose one
//     again without an answer to that. The wake below is procedural and avoids
//     it by the SHAPE of its sample domain rather than by hiding the period:
//     high angular frequency against a low radial one gives features long in the
//     radial direction, which is what a tail off a front actually looks like.
//
// ── WHAT IT IS NOW ──────────────────────────────────────────────────────────
//
// Two things, and no texture at all:
//
//   THE FRONT — a thin band riding the outer side of the section, hard on its
//   leading face and trailing behind it, irregular in radius at two frequencies,
//   varying in thickness along its length, burning in arcs, and gone well before
//   the ring has finished dispersing. It is the effect's identity: a torn ring
//   of burning cloud can be beautiful and still not read as a SHOCK, because
//   nothing in it says which way it is going.
//
//   THE WAKE — what the front has already burned, trailing inward, torn into
//   strands by a threshold that climbs quadratically. It is also what the mesh's
//   flared bell is drawn on: the bell opens INWARD of the front (see
//   ShockRing_Flare), so with nothing behind the line the entire
//   three-dimensional section would be invisible.
//
// CELLS ARE SQUARE IN THE WORLD, NOT IN UV. u spans the whole circumference
// (2*PI*R), v spans the canvas (0.66*R), so a cell square in UV is nine times
// wider than tall in metres. Every angular frequency here is chosen knowing
// that; where an elongated feature is wanted it is obtained on purpose.
//
// Everything is driven by u_t01, never by the wall clock: two rings alive at
// different phases must not share a pattern, and a ring must look identical
// wherever the frame lands in its life. (shock_ring_test.c enforces this by
// grepping for the wall-clock uniform's name, so do not name it in a comment
// either.)

uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_emission;
uniform float u_t01;   // 0 -> 1 over the ring's life, drives the tear and the fade
uniform float u_coreV; // where the section's centre-line sits across the canvas
uniform float u_seed;  // per-ring, so two rings are not the same ring
uniform float u_hole;  // v below this is cut away; the size of the empty middle
uniform float u_premultiply; // 0 = BODY (BLEND_ALPHA), 1 = EMISSION (premultiplied)

// Periodic value noise. `period` counts CELLS of x. The ring's u wraps at 1.0,
// so a hash sampled at u * N must wrap at N — the shared vnoise() does not, and
// the mismatch between hash(0) and hash(N) draws a bright line down one radius
// of EVERY ring, in the same place every time. Cheap to prevent, invisible to
// diagnose once a dozen other things are also moving.
float ShockNoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    float x0 = mod(i.x, period);
    float x1 = mod(i.x + 1.0, period);
    float y0 = i.y;
    float y1 = i.y + 1.0;
    return mix(mix(hash2(vec2(x0, y0)), hash2(vec2(x1, y0)), w.x),
               mix(hash2(vec2(x0, y1)), hash2(vec2(x1, y1)), w.x), w.y);
}

// Octaves double the frequency AND the period together, so every octave stays
// closed at the seam. Doubling only the frequency reopens it at octave two.
float FbmRing(vec2 p, float period, int octaves)
{
    float sum = 0.0, amp = 0.5, tot = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i >= octaves) break;
        sum += amp * ShockNoise(p, period);
        tot += amp;
        p *= 2.0;
        period *= 2.0;
        amp *= 0.5;
    }
    return sum / tot;
}

void main()
{
    float u = fragTexCoord.x; // around the ring, 0 -> 1, wraps
    float v = fragTexCoord.y; // across the canvas, 0 = inner base, 1 = outer

    // ── 1. THE CENTRE-LINE, THE WIDTH, THE LOBES ────────────────────────────
    // All low frequency, all angular. This is where the ring stops being a
    // circle: the centre-line wanders, the section breathes, and a few arcs
    // throw everything much further out than the rest.
    float wob = FbmRing(vec2(u * 3.0, 7.0 + u_seed), 3.0, 2) - 0.5;
    float widN = FbmRing(vec2(u * 5.0, 19.0 + u_seed), 5.0, 2);
    // THE AMPLITUDE HAS TO BE READ IN RADIUS, NOT IN v. `wob` is a two-octave
    // FbmRing, whose normalised range is roughly ±0.25 rather than ±0.5, so at
    // 0.26 the centre-line moved ±0.065 of the canvas — about 7% of the ring's
    // radius, which is invisible. At 0.42 it is ±0.11 of the canvas, ~12% of the
    // radius, and the outline stops reading as a drawn circle.
    float coreV = u_coreV + wob * 0.42;
    float widA = mix(0.18, 0.30, u_t01) * mix(0.42, 1.55, widN);

    // A handful of arcs where the front is thrown much further out than the
    // rest. LOW frequency on purpose: at 4 cells around the whole ring this is
    // three or four big irregular tongues, not a scallop pattern — the moment
    // this number goes up it stops being an explosion and becomes a doily.
    float lobeN = FbmRing(vec2(u * 4.0, 61.0 + u_seed), 4.0, 2);
    float lobe = smoothstep(0.40, 0.78, lobeN);
    // They GROW with the ring: at release everything is still travelling
    // together, and the tongues only separate as the front decelerates.
    float outGain = 1.0 + lobe * 2.60 * (0.30 + u_t01);

    // Where the ring is thick and where it is thin, at a scale larger than
    // anything else here.
    float clump = FbmRing(vec2(u * 5.0, 3.0 + u_seed), 5.0, 2);

    // ── 2. THE FRONT ────────────────────────────────────────────────────────
    //
    // NOT AN ISO-CONTOUR. The band is taken on `v`, the canvas COORDINATE, which
    // is monotonic across the section — one crossing, one line, by construction.
    // Taken on a density field instead, any level below the peak is crossed
    // twice (up one side of the hump and down the other) and the ring
    // double-edges no matter how it is tuned.
    //
    // It must not be a circle, and three separate things keep it off one: it
    // rides the wandering centre-line above, it carries a slow and a fast radial
    // wander of its own, and its thickness varies along its length.
    float frontN = FbmRing(vec2(u * 9.0, 83.0 + u_seed), 9.0, 2) - 0.5;
    float frontF = FbmRing(vec2(u * 23.0, 151.0 + u_seed), 23.0, 2) - 0.5;
    float frontV = coreV + widA * outGain * (0.58 + frontN * 1.30 + frontF * 0.70);

    float rimT = FbmRing(vec2(u * 6.0, 101.0 + u_seed), 6.0, 2);
    float rimW = widA * outGain * mix(0.09, 0.22, u_t01) * mix(0.40, 1.85, rimT);

    // ASYMMETRIC, AND THIS IS THE WHOLE DIFFERENCE BETWEEN A SHOCK FRONT AND A
    // GLOWING WIRE. Symmetric, the band is a tube: equally soft on both sides,
    // so it reads as a rope laid on the ring and nothing in it says which way it
    // is travelling. A real front is a discontinuity — hard on the leading side,
    // with everything it has already burned trailing behind it.
    float dR = v - frontV;
    float rimOut = max(rimW * 0.34, 0.005);
    float rimIn = max(rimW * 1.30, 0.011);
    float rimX = clamp((dR > 0.0) ? dR / rimOut : -dR / rimIn, 0.0, 1.0);
    float rimBand = 1.0 - rimX;
    rimBand *= rimBand;

    // AND IT BURNS IN ARCS. Measured on its own, an evenly bright front is a
    // neon hoop. The gate reaches 0.10, i.e. some arcs have no leading edge.
    float rimArc = smoothstep(0.18, 0.72,
                              FbmRing(vec2(u * 5.0, 127.0 + u_seed), 5.0, 2));
    // A FRONT OUTLIVES NOTHING. Faded on t01^2 it was still at 47% of full
    // brightness three quarters of the way through the life, so the late ring
    // read as a thick unbroken rope. pow(1 - t, 2.4) has it gone by about 0.7.
    float rimLife = pow(max(1.0 - u_t01, 0.0), 2.4);
    float rim = rimBand * rimLife * mix(0.55, 1.00, clump) * mix(0.10, 1.35, rimArc);

    // ── 3. THE WAKE ─────────────────────────────────────────────────────────
    //
    // WHY THIS EXISTS AT ALL. The authored smoke sheet was removed on
    // 26/08/2026 by owner decision — everything the ring showed used to come out
    // of it, and what is left is the front. But a front on its own is a flat
    // glowing loop: the bell the mesh sweeps (see SHOCK_FLARE_RATIO) is zero at
    // the crest and opens INWARD of the front, so with nothing drawn behind the
    // line the whole three-dimensional section is invisible. The wake is the
    // front's own trailing tail — the material it has already burned through —
    // and it is what the bell is drawn ON.
    //
    // It is entirely procedural. That was the failure mode of the field this
    // replaced ("an fbm is statistically homogeneous, so eroding one gives
    // features of one size evenly distributed — a necklace"), and the reason it
    // is not that here is the SHAPE of the sample domain: the u frequency is
    // high and the radial one is low, so features come out long in the radial
    // direction. Streaks trailing off a front, not blobs on a band.
    float wakeN = FbmRing(vec2(u * 17.0, 199.0 + u_seed), 17.0, 2);
    float wakeM = FbmRing(vec2(u * 8.0, 211.0 + u_seed), 8.0, 2);
    // The tail LENGTHENS over the life, and it is a different length at every
    // angle — that variation is what makes it read as torn rather than as an
    // annulus of constant width.
    // LENGTH IS IN v UNITS AND v ONLY SPANS 1.0. Written as widA * outGain *
    // 2.6 * 1.6 the tail reached 1.5 — one and a half canvases — so it ran past
    // the hole, past the inner base, and the ring rendered as a filled blob with
    // a bright outline. It has to stay a FRACTION of the section.
    float wakeLen = widA * outGain * mix(1.30, 1.70, u_t01) * mix(0.35, 1.55, wakeN);
    // A SECOND, FASTER VARIATION IN LENGTH. With only `wakeN` (17 cells) the
    // tail's inner boundary is a smooth scalloped curve — the fringe reads as
    // pleats cut in cloth rather than as strands of different lengths.
    wakeLen *= mix(0.62, 1.45, FbmRing(vec2(u * 11.0, 233.0 + u_seed), 11.0, 2));
    // AND IT IS CAPPED. Late in the life widA, outGain and the two length
    // noises all peak together and the tail reaches most of the canvas — at
    // which point the ring is not a ring, it is a BOWL seen at an angle, with
    // the flared bell filling what should be the empty middle. That is exactly
    // the "flower/shell" this effect kept turning into. The empty centre is
    // part of the silhouette, not a leftover.
    wakeLen = min(wakeLen, 0.36);
    float wx = clamp((frontV - v) / max(wakeLen, 0.03), 0.0, 1.0);
    float fall = 1.0 - wx;
    // ── THE FIELD IS SAMPLED ON ABSOLUTE DEPTH, NOT ON `wx` ─────────────────
    // `wx` is normalised by wakeLen, so sampling the grain on it ties the
    // feature size to the tail's LENGTH: as the tail grows over the life the
    // same features are stretched with it, and the late ring — which is where
    // the tail is longest — came out as broad smeared strokes with no internal
    // detail at all, like torn paper. `wd` is how far behind the front the
    // fragment is in canvas units, so a strand stays the same width whether the
    // tail is short or long. Only the falloff and the shear stay on `wx`,
    // because those really are proportional to the tail.
    float wd = max(frontV - v, 0.0);
    float wake = (dR > 0.0) ? 0.0 : fall * fall;
    // ── AND THIS IS WHERE THE STRUCTURE KEPT DISAPPEARING ───────────────────
    //
    // Written as `FbmRing(..., 3) * 0.66 + FbmRing(..., 2) * 0.42` this field
    // measured essentially CONSTANT — a debug pass that painted it straight out
    // came back flat grey, roughly 0.54 ± 0.08. Both halves of that are the same
    // mistake: averaging. Each octave of a value noise is an independent sample
    // near 0.5 and the fbm divides by the total amplitude, so three octaves
    // already sit in a narrow band around the mean; summing two such fields
    // narrows it again. Against a field that narrow, every threshold written for
    // 0..1 stops carving and becomes an on/off switch for the whole ring — which
    // is exactly how this tail behaved: smooth airbrushed band at one setting,
    // bare wire 0.18 higher, nothing in between.
    //
    // TWO OCTAVES, STRETCHED, AND THE DETAIL MULTIPLIED IN RATHER THAN ADDED.
    // Multiplication keeps contrast; addition is the averaging that destroyed it.
    //
    // Sampled with a HIGH angular and a LOW radial frequency, so the features
    // come out long in the radial direction — streaks trailing off a front, not
    // blobs on a band. That domain shape is also why a procedural field is
    // acceptable here where the one it replaced was a "necklace": it is not a
    // warp trying to hide a period.
    // ── SHEAR, WHICH IS WHAT STOPS THE TONGUES BEING PLEATS ─────────────────
    //
    // Sampled on `u` directly, every feature runs dead straight along a radius,
    // and thirty of them evenly spaced around a ring is a pleated skirt however
    // irregular each one is. Real smoke behind a ring front does not travel
    // radially — the ring is a vortex and the material shears tangentially as it
    // falls behind. Offsetting the sample angle by a function of how far back it
    // is makes the strands sweep and curl, and it costs one fbm.
    //
    // MEASURE THE OFFSET AGAINST THE FEATURE SPACING, NOT AGAINST 1.0. It is in
    // u units — fractions of the WHOLE circumference — and at 30 angular cells
    // one feature is 1/30 = 0.033 of that. Written as 0.045 + 0.030 scaled by a
    // noise reaching 0.95, the offset peaked near 0.071: more than TWO feature
    // widths, so every strand was dragged sideways past its neighbour and the
    // late tail rendered as long tangential smears — torn paper, not smoke.
    // Held to about two thirds of one cell it bends the strands instead.
    // (U_PER_V is the other half of this: an angular displacement here is nine
    // times the metres a radial one of the same number would be.)
    float shearN = FbmRing(vec2(u * 3.0, 71.0 + u_seed), 3.0, 2) - 0.5;
    float uw = u + (wx * 0.014 + wx * wx * 0.010) * (0.55 + 1.60 * shearN);

    // THREE SCALES, MULTIPLIED. The tail had two and read as smooth wedges: a
    // single coarse field decides the shape and there is nothing inside it, so
    // every tongue is a flat plane of colour. Detail has to be present at the
    // scale of the tongue AND inside it. Multiplied, not summed — summing is the
    // averaging that flattened this field once already, and each factor here has
    // a mean of 1.0 so the product keeps the mean and multiplies the variance.
    // THE RADIAL FREQUENCY IS SET AGAINST THE TAIL'S LENGTH, and getting it
    // wrong is what made the late ring read as torn paper. At 3.2 cycles per
    // unit of v, one feature spans 1.1 cells across a tail 0.36 long — i.e. a
    // single feature covers the WHOLE tail radially, so every strand is one
    // unbroken wedge from the front to the inner edge. At 7.0 a strand is about
    // 40% of the tail: long enough to read as a streak, short enough to break up
    // along its own length. Still far below the angular frequency, which is what
    // keeps the features radial rather than round.
    float streak = FbmRing(vec2(uw * 30.0, wd * 7.00 + 31.0 + u_seed), 30.0, 2);
    float mid = FbmRing(vec2(uw * 68.0, wd * 15.0 + 53.0 + u_seed), 68.0, 2);
    float fine = FbmRing(vec2(uw * 148.0, wd * 30.0 + 97.0 + u_seed), 148.0, 1);
    // THE STRETCH IS A CONTRAST KNOB WITH A CLIFF AT BOTH ENDS. Too little and
    // the field sits in a narrow band around 0.5, as above. Too much and it
    // clamps BIMODAL — mostly exact 0 and exact 1 — at which point the threshold
    // has nothing left to bite on either, and sweeping the tear from 0.30 to
    // 0.92 across the ring's whole life changed the picture almost not at all.
    // 1.9 on a two-octave field leaves roughly 0.15..0.85, graded, which is what
    // a climbing threshold needs in order to erode something gradually.
    float grain = clamp((streak - 0.5) * 1.90 + 0.5, 0.0, 1.0);
    grain = clamp(grain * (0.62 + 0.76 * mid) * (0.74 + 0.52 * fine), 0.0, 1.0);

    // TWO USES OF THE SAME FIELD, AND THEY MUST BE SEPARATE TERMS. Thresholding
    // alone gives no structure at all: pushed through one smoothstep the field
    // saturates, so the threshold stops carving and starts acting as an on/off
    // switch for the whole ring — one setting rendered a smooth airbrushed band,
    // and a setting 0.18 higher rendered a bare wire. The first term is
    // permanent shading (the tail is never uniform), the second is the tear.
    // THE FLOOR IS A WASH. At 0.14 there is coverage EVERYWHERE in the tail no
    // matter what the field says, and that constant term is a smooth gradient
    // under all of the structure — from a distance the strands disappear into it
    // and the tail reads as a soft painted mass. 0.03 leaves the field in charge
    // of where the tail exists at all.
    wake *= 0.03 + 0.97 * smoothstep(0.30, 0.86, grain);
    // THE TEARING climbs quadratically, so the tail comes apart from a
    // continuous skirt into separate strands as the ring ages, which is the
    // whole late-life read.
    float t2 = u_t01 * u_t01;
    float tear = mix(0.18, 0.86, t2) + (clump - 0.5) * 0.30;
    wake *= smoothstep(tear, tear + 0.14, grain);
    wake *= mix(0.30, 1.00, wakeM) * mix(0.40, 1.00, rimArc);
    // FOUR MULTIPLICATIVE GATES EACH AVERAGING A HALF LEAVE A SIXTEENTH. The
    // radial falloff, the grain shading, the tear and the two angular gates are
    // all fractions, and their product put the tail at ~0.06 coverage — present
    // in the arithmetic and invisible on screen, so the ring looked like a bare
    // front with nothing behind it. Gained back up and clamped, deliberately, in
    // one place rather than by inflating each gate until none of them gates.
    wake = clamp(wake * 3.40, 0.0, 1.0) * mix(1.0, 0.55, u_t01);

    // Hot filaments inside the tail: the top of the grain range, thinned. A wide
    // ramp shades the whole tail and cannot BE its bright centres.
    float ember = pow(smoothstep(0.74, 0.98, grain), 2.0) * wake *
                  mix(1.0, 0.35, u_t01);

    // ── 4. THE VALUE LADDER ─────────────────────────────────────────────────
    // 1 at the front, 0 at the far end of the tail. A shock ring's brightness
    // lives on the RADIUS: lighting the section evenly is what makes one read as
    // a printed decal however good its internal detail is.
    float heat = 1.0 - wx;

    // The canvas fade is taken on the RAW v: the mesh has a hard boundary at
    // v = 0 and v = 1, and any coverage surviving to it is clipped by geometry
    // into a straight chord — a polygon edge drawn across the smoke.
    float edge = smoothstep(0.0, 0.05, v) * (1.0 - smoothstep(0.93, 1.0, v));
    // THE HOLE. The one explicit control over how big the empty centre is,
    // independent of how far the tail reaches, exposed as the live tunable
    // `shock_hole` because it is a judgement call, not a derivation.
    edge *= smoothstep(u_hole * 0.35, u_hole, v);

    // ── 5. THE TWO PASSES CARRY DIFFERENT SIGNALS ───────────────────────────
    //
    // The BODY pass is pigment: saturated element hue, which is what lets the
    // ring attenuate bright scenery instead of only adding to it. The EMISSION
    // pass is light, driven by the front and the tail's hot filaments, with a
    // gain that clears main.c's bloomThreshold of 1.25 — see the note in
    // vc_shock_ring.inl for the arithmetic, which is the whole reason this
    // effect did not glow for as long as it did not.
    if (u_premultiply > 0.5)
    {
        // THE TAIL MUST NOT BE LIT AS A MASS. At 0.30 with a gain of 7 every
        // fragment of it arrives at 2.1 in HDR — clipped, so the whole skirt
        // tone-maps to one flat saturated orange and every bit of structure in
        // the field behind it is thrown away. The tail is lit by its FILAMENTS
        // (`ember`); the mass itself only needs enough to sit under them.
        float glowMask = clamp(rim * 2.80 + ember * 1.15 + wake * 0.08, 0.0, 1.0) *
                         edge * u_opacity * u_bodyColor.a;
        // COVERAGE AND BRIGHTNESS ARE SEPARATE ARGUMENTS HERE, and that is the
        // reason this calls the six-argument resolver. Delivered as
        // bodyColor * intensity * a the two are tied together: the only way to
        // reach the bloom threshold is near-opacity, and the premultiplied
        // dst*(1-a) term then ERASES the body pass drawn underneath.
        float ga = clamp(rim * 0.42 + ember * 0.30 + wake * 0.14, 0.0, 1.0) *
                   edge * u_opacity * u_bodyColor.a;
        // WHITE ONLY AT THE FRONT. Whitening the whole pass is what makes an
        // effect vanish on a white plate; the hue has to survive everywhere the
        // front is not (BRIGHT_BACKGROUND_VFX_SPEC.md §5).
        vec3 hotCol = mix(u_glowColor.rgb, vec3(1.0),
                          clamp(rim * 0.40 + ember * 0.10, 0.0, 1.0));
        finalColor = VFX_ResolvePremultiplied(hotCol, u_emission * 0.30, ga,
                                              hotCol, glowMask, u_emission);
    }
    else
    {
        float cover = clamp(rim * 0.90 + wake * 1.05 + ember * 0.55, 0.0, 1.0) * edge;
        // THE TAIL COOLS BEHIND THE FRONT. Deep ember at the far end, bright
        // where the material is still travelling with the edge.
        vec3 coal = u_bodyColor.rgb * mix(0.16, 1.00, heat * heat);
        vec3 col = mix(coal, u_glowColor.rgb,
                       clamp(ember * 0.45 + rim * 0.75, 0.0, 1.0));
        float a = cover * u_opacity * u_bodyColor.a;
        // ONE formula per blend state, never one for both. BLEND_ALPHA is
        // (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), so the body pass hands over straight
        // RGB and the hardware applies coverage; BLEND_ALPHA_PREMULTIPLY is
        // (ONE, ONE_MINUS_SRC_ALPHA) and does not, which is why the branch above
        // multiplies by its own alpha itself.
        finalColor = VFX_ResolveBody(col, u_emission, a);
    }
}
