#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── VFX_ComposeShockRing: two closed smoke ropes, eroded open ────────────────
//
// THE WISPS ARE THE TEXTURE. Light the authored smoke directly — its alpha
// raised to a power gives hot cores inside thin wisps and a soft falloff around
// them — and use the erosion threshold ONLY to tear the band open over time.
//
// AN ISO-CONTOUR IS THE WRONG TOOL HERE AND FAILS IN A VERY SPECIFIC WAY. An
// earlier version lit the narrow band where the field crossed the threshold,
// {dens == thr}. That reasoning is sound for a procedural field, and the level
// set of an fbm really is the thin meandering branching filament reference
// footage shows. But across the rope the density is a single HUMP: it rises
// from nothing at the inner edge, peaks at the centre-line, and falls to nothing
// at the outer edge. Any threshold below the peak is therefore crossed TWICE —
// once on the way up and once on the way down — so the contour draws an inner
// rim AND an outer rim, and the ring reads as double-edged no matter how many
// ropes there are. It is not a leftover second rope; it is what a level set of a
// hump has to look like.
//
// With an authored sheet none of that is needed: the wisps are already the
// shape, so they only have to be lit.
//
// ONE ROPE. There was a second, thinner one riding outside it, on the reading
// that reference footage shows an inner dense ring and an outer one separating
// from it. Judged against the render that outer rope was wrong — the inner
// one's behaviour was right on its own and the outer only muddied it. If it
// comes back it should be a second CALL at a different radius and phase, not a
// second lobe sharing this one's field: sharing the field is what made the two
// move as one object instead of as two.
//
// THE TWO FORCES OVERLAP OVER THE LIFE. Expansion is strongly ease-out and
// front-loaded, while quadratic erosion begins during that release.  The front
// therefore settles at its final radius as the smoke continues to reshape,
// rather than becoming a static clean ring before it tears apart.
//
// WHERE THE TENDRILS COME FROM. The sample space is COMPRESSED radially as the
// ring expands, so one feature of the field covers more and more of the band as
// it travels. The wisps are stretched smoke, and they lengthen over the ring's
// life without anything animating a length.
//
// THE SHAPE IS AUTHORED, NOT PROCEDURAL, and this is the last thing that made
// the ring read as machine-made. An fbm is STATISTICALLY HOMOGENEOUS: every
// region of it looks like every other region, so eroding one gives features of
// one size, evenly distributed, all the way round — a necklace. Real reference
// footage has long sweeping strokes next to fine detail next to nothing at all,
// which is a property of authored or simulated data and cannot be recovered by
// clumping noise. Lowering the base frequency trades a fine necklace for a
// coarse one; it does not remove the necklace.
//
// So `texture0` is a thin-smoke strip (VFX_SURFACE_SHOCK_RING_SMOKE) mapped ONCE
// around u — a SHAPE, never a tiled material — and its alpha is the density
// field everything below reads. Noise still runs, but only to DISTORT the UVs
// and to break the boundary. This mirrors the reference material setup exactly:
// Tiling 1.0/1.0, a smooth-wave distortion texture, and a pan along the strip's
// short axis so the smoke flows outward across the band.
//
// The procedural field is kept as the sheet-missing fallback, so a lost asset
// degrades to a visibly worse ring rather than to nothing.
//
// WHAT THIS REPLACED. A generative version that cut the angular coordinate into
// cells and grew one strand per cell. That has a comb built into it: one feature
// per cell means the features are evenly spaced BY DEFINITION, and the eye reads
// the spacing long before it reads any per-strand variation. Domain warp and
// per-strand jitter made it a prettier comb — eyelashes rather than spokes —
// because none of them touch the period. Do not propose anything generative here
// again without an answer to that.
//
// CELLS ARE SQUARE IN THE WORLD, NOT IN UV. u spans the whole circumference
// (2*PI*R), v spans the canvas (0.66*R), so a cell square in UV is nine times
// wider than tall in metres and the field produces smeared blobs. Every angular
// frequency and amplitude below is converted through U_PER_V. See
// core/docs/LANDMINES.md, "In a polar UV, u and v are not the same scale".
//
// Everything is driven by u_t01, never by the wall clock: two rings alive at
// different phases must not share a pattern, and a ring must look identical
// wherever the frame lands in its life. (shock_ring_test.c enforces this by
// grepping for the wall-clock uniform's name, so do not name it in a comment
// either.)

uniform sampler2D texture0; // the authored thin-smoke strip, mapped ONCE around u
uniform int   u_hasSmoke;   // 0 = sheet missing, fall back to the procedural field
uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_emission;
uniform float u_t01;    // 0 -> 1 over the ring's life, drives erosion and stretch
uniform float u_detail; // EVEN INTEGER angular cell count; the u-period of the noise
uniform float u_coreV;  // where the inner rope sits across the canvas
uniform float u_seed;   // per-ring, so two rings are not the same ring
// HIGH / MID / LOW. One master shader, three instances — the reference workflow
// (Thomas Pluys, 80.lv). `u_layerDetail` above 1 sharpens (thinner wisps, higher
// angular sample rate); below 1 smooths. `u_layerPhase` slides the sheet around
// u so the three layers' torn boundaries never coincide, which is the entire
// point: one layer has one silhouette and no setting gives both a crisp edge and
// a soft one.
uniform float u_layerPhase;
uniform float u_layerDetail;
uniform float u_hole; // v below this is cut away; the size of the empty middle

// u spans 2*PI*R, v spans SHOCK_CANVAS_MUL * SHOCK_CORE_RATIO * R = 0.66*R.
const float U_PER_V = 9.5;

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

// One closed rope: 1 at its centre-line, 0 at its edges. `w` is its half-width
// in v units.
float Rope(float v, float centre, float w, float u_t01)
{
    // Squared falloff, soft from the centre out. A rope with a defined edge
    // reads as a painted band and the erosion then looks like cracks in paint
    // rather than like smoke thinning out.
    // ASYMMETRIC: a longer tail INWARD. Smoke thrown outward still trails back
    // towards where it came from, and a rope that falls off symmetrically leaves
    // a clean circular hole in the middle that no reference has.
    // A new front throws a broad trail back toward its centre, closing the
    // oversized hollow that a symmetric rope leaves.  As the ring settles that
    // tail retracts before it can turn the late effect into a filled disc.
    float d = v - centre;
    float reach = (d < 0.0) ? w * mix(1.42, 1.15, u_t01) : w;
    float x = clamp(abs(d) / max(reach, 0.02), 0.0, 1.0);
    float f = 1.0 - x * x;
    return f * f;
}

void main()
{
    float u = fragTexCoord.x; // around the ring, 0 -> 1, wraps
    float v = fragTexCoord.y; // across the canvas, 0 = inner base, 1 = outer

    float nU = u_detail;       // angular cells, even integer
    float nV = nU / U_PER_V;   // radial cells at the SAME world size

    // ── 1. THE TWO CLOSED ROPES ─────────────────────────────────────────────
    // Before anything is taken away these are continuous, unbroken bands. The
    // centre-line wanders and the widths breathe at low frequency — the DEFORM
    // step, and what stops the inner boundary reading as a circle once the
    // erosion starts.
    // THE OUTER BOUNDARY IS WHAT READS AS A CIRCLE. The eye locks onto the
    // silhouette long before it reads any surface detail, so a centre-line and a
    // width that barely vary give a perfectly round ring however torn its inside
    // looks. Both wander much harder now, and the width uses its own frequency so
    // the widest arcs are not the outermost ones.
    float wob = FbmRing(vec2(u * 3.0, 7.0 + u_seed), 3.0, 2) - 0.5;
    float widN = FbmRing(vec2(u * 5.0, 19.0 + u_seed), 5.0, 2);
    float coreV = u_coreV + wob * 0.26;

    // A front spreads as it travels. The rope must also stay at least as wide as
    // a noise cell, or the contour has no room to wander radially and every
    // feature comes out the same size.
    // The band widens as the front spreads, but only a little. At 0.50 it more
    // than doubles as a FRACTION of a canvas that is itself already growing with
    // the radius — so the annulus swallows its own middle and the ring ends its
    // life as a filled disc, which is the opposite of dispersing.
    float widA = mix(0.18, 0.30, u_t01) * mix(0.42, 1.55, widN);
    float ropeA = Rope(v, coreV, widA, u_t01);

    // ── 2. EXPANSION ────────────────────────────────────────────────────────
    // The sample space is COMPRESSED radially as the ring grows, so a feature
    // that covered 0.1 of the band early covers 0.3 of it late: it is being
    // pulled outward. The tendrils are this, and nothing else.
    // 0.16 compresses the sample space more than six times by the end, which
    // stops reading as stretched smoke and starts reading as a STARBURST: the
    // sheet's texels are smeared into straight radial rays. 0.40 still lengthens
    // the wisps visibly without turning them into rays.
    float vs = coreV + (v - coreV) * mix(1.0, 0.40, u_t01);
    // This is a coverage warp, never vertex displacement.  It is already
    // present at birth so the small closed ring is smoky rather than analytic,
    // then grows as the front settles.  Keep it radial: a comparable angular
    // displacement would become a multi-metre tangential smear in polar UV.
    float radialRuffle = (FbmRing(vec2(u * 6.0, 47.0 + u_seed), 6.0, 2) - 0.5) *
                          mix(0.035, 0.14, u_t01);
    vs += radialRuffle;

    // ── 3. MOTION NOISE ─────────────────────────────────────────────────────
    // A slow angular drift growing with the ring's life, so the torn pieces
    // slide against each other instead of expanding rigidly. The amplitude is in
    // v units and converted — unconverted, 0.55 here would be five metres.
    float drift = FbmRing(vec2(u * 3.0, 31.0 + u_seed), 3.0, 2) - 0.5;
    float us = u + drift * 0.85 * (0.35 + u_t01) / U_PER_V;

    // ── 4. THE FIELD ────────────────────────────────────────────────────────
    // The strip maps ONCE around u. Its short axis is the rope's own
    // cross-section, PANNED outward over the ring's life: the smoke slides
    // across the band and off it, which is both the outward flow and the
    // dissipation, for one multiply. Sampling is clamped, and the sheet's top
    // and bottom rows are empty, so panning past the edge simply runs out of
    // smoke instead of repeating it.
    float pan = -0.46 * u_t01;

    // THREE LAYERS AT COPRIME ANGULAR SCALES. The sheet is authored as a single
    // non-tiling strip, and mapping it once around a ring this size stretches
    // 512 texels over the whole circumference — the texels become visible as
    // stair-stepping along every edge. Sampling it 2x, 3x and 5x instead puts
    // real texels back on screen, and because 2, 3 and 5 are coprime the
    // combined pattern only repeats after thirty revolutions, so it still reads
    // as one non-repeating strip rather than as tiled wallpaper.
    //
    // Each layer also reads a DIFFERENT, NARROWER slice of the sheet's height,
    // stretched across the rope's full width. That is what turns the sheet's
    // compact puffs into the long sweeping streaks the reference material shows:
    // it is the same smoke, scaled anisotropically.
    // FLOOR THE DIVISOR, not just guard it against zero. Early in the life widA can
    // fall to 0.08, and dividing by that magnifies the sheet six times across the
    // band — the texels smear into radial streaks converging on the centre, which
    // reads as a starburst rather than as smoke. The floor caps how far the sheet
    // may ever be stretched.
    float tvA = 0.5 + (vs - coreV) / max(widA * 2.2, 0.26) + pan;
    float tvB = 0.5 + (vs - coreV) / max(widA * 3.4, 0.38) + pan * 0.7 + 0.21;

    // The sheet is PERIODIC IN X, so it wraps directly. The previous strip was
    // not, and its blank ends had to be remapped away with a 0.06..0.94 squeeze
    // — a workaround for a texture that should simply have tiled. Making the
    // generator wrap its noise lattice and its particles removed the need.
    // u_layerDetail MUST NOT scale the angular sample rate. Scaling it down for
    // the LOW layer leaves barely one repeat of the sheet around the entire
    // circumference, so each texel column is magnified into a wide radial band
    // and the ring reads as a STARBURST. The layer differences belong in the
    // range remap below, which changes how the sheet is READ, not how far it is
    // stretched. Rates are 4 and 7 — coprime, so their combination does not
    // repeat, and high enough that texels stay smaller than the wisps.
    float tuA = fract(us * 4.0 + u_layerPhase * 0.11);
    float tuB = fract(us * 7.0 + u_layerPhase * 0.17 + 0.37);

    float sA = texture(texture0, vec2(tuA, clamp(tvA, 0.002, 0.998))).a;
    float sB = texture(texture0, vec2(tuB, clamp(tvB, 0.002, 0.998))).a;
    // WEIGHTED SUM, NOT max, AND ONLY TWO LAYERS. Three layers combined with max
    // saturate — every fragment takes whichever sheet is densest there, so the
    // rope fills in solid — and the third layer's 5x angular scale put more
    // texels on screen than the sheet has, which broke the whole ring into
    // speckle. Two coprime scales summed keeps the density low and the gaps real.
    float sheet = clamp(sA * 0.85 + sB * 0.55, 0.0, 1.0);
    // REMAP THE BAND THE DATA ACTUALLY OCCUPIES. This sheet is thin smoke:
    // three quarters of it is empty and most of the rest sits under 0.3, so
    // every threshold downstream — the erosion, the hot-core curve — was
    // measuring against a range the field never reaches, and the ring rendered
    // as a handful of stray flecks with no single term looking wrong. The
    // constants below are written against a full-range field; this is what makes
    // that true. Re-tune this line, not them, when the sheet is regenerated.
    // Sharper layers keep a narrower slice of the range, so their wisps come out
    // thin and hard; smoothed layers open it up into broad soft mass.
    sheet = smoothstep(0.03 + 0.06 * (u_layerDetail - 1.0),
                       0.45 - 0.14 * (u_layerDetail - 1.0), sheet);

    // Procedural fallback only. Kept deliberately, because a missing sheet must
    // degrade to a worse ring and not to an empty one.
    float fine   = FbmRing(vec2(us * nU, vs * nV + u_seed), nU, 3);
    float coarse = FbmRing(vec2(us * nU * 0.5, vs * nV * 0.5 + 13.0 + u_seed),
                           nU * 0.5, 2);
    float proc = fine * 0.58 + coarse * 0.42;

    float dens = mix(proc, sheet, float(u_hasSmoke));

    // ── 4b. CLUMPING ────────────────────────────────────────────────────────
    // Still worth having on top of an authored sheet: it varies WHERE the rope
    // tears first, at a scale larger than anything in the sheet.
    float clump = FbmRing(vec2(u * 5.0, 3.0 + u_seed), 5.0, 2);

    // ── 5. THE EROSION ──────────────────────────────────────────────────────
    // Quadratic starts reshaping the smoke while the front still travels.  The
    // low initial threshold keeps that first tiny ring closed; later it opens
    // unevenly into the fading fragments seen after the radius has settled.
    float t2 = u_t01 * u_t01;
    // The clump offset staggers tearing around the ring.  Keep it narrow enough
    // that the newborn smoke band remains closed rather than popping into arcs.
    float clumpT = (clump - 0.5) * 0.28;
    float thrA = mix(0.02, 0.82, t2) + clumpT;

    // The rope itself is thin in some arcs and full in others.
    ropeA *= mix(0.55, 1.00, clump);

    // The tear is an ALPHA CUT, not a lit band: the threshold decides what still
    // exists, and what exists is then lit by its own density.
    float alive = ropeA * smoothstep(thrA, thrA + 0.20, dens);

    // Hot cores inside the wisps, soft everywhere else. This is the reference
    // material's HDR intensity on the smoke texture, not a rim.
    //
    // SHAPED, NOT POWERED. The sheet's alpha lives mostly between 0.3 and 0.6 —
    // it is thin smoke — so pow(dens, 2.6) maps almost all of it to under 0.2
    // and the whole ring goes dim and sparse. A smoothstep across the range the
    // data actually occupies uses the full output range instead of the top of it.
    // THE HOT CORES COOL. Young smoke has bright dense cores; old smoke is a
    // diffuse mass with none. Holding this constant is what made the late ring
    // the brightest thing on screen when it should have been the faintest.
    float hot = alive * smoothstep(0.26, 0.78, dens) * mix(1.0, 0.30, u_t01);
    float smoke = alive * smoothstep(0.04, 0.46, dens);

    // The canvas fade is taken on the RAW v: the mesh has a hard boundary at
    // v = 0 and v = 1, and any coverage surviving to it is clipped by geometry
    // into a straight chord — a polygon edge drawn across the smoke.
    float edge = smoothstep(0.0, 0.05, v) * (1.0 - smoothstep(0.93, 1.0, v));

    // THE HOLE. Three overlapping layers each reach inward, so between them they
    // close the middle completely — the ring becomes a disc. This is the one
    // explicit control over how big the empty centre is, independent of how many
    // layers there are or how wide the canvas is, and it is exposed as the live
    // tunable `shock_hole` because it is a judgement call, not a derivation.
    edge *= smoothstep(u_hole * 0.35, u_hole, v);

    // The body is DIM and the rim is hot. Lighting the body is what makes this
    // read as fire instead of as smoke with burning edges.
    float cover = clamp(smoke * 0.60 + hot * 0.95, 0.0, 1.0) * edge;
    vec3 col = mix(u_bodyColor.rgb, u_glowColor.rgb, hot);

    finalColor = VFX_ResolveBody(col, u_emission,
                                 cover * u_opacity * u_bodyColor.a);
}
