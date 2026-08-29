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

// Periodic value noise with Quintic C2 continuous interpolation.
float ShockNoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float x0 = mod(i.x, period);
    float x1 = mod(i.x + 1.0, period);
    float y0 = i.y;
    float y1 = i.y + 1.0;
    return mix(mix(hash2(vec2(x0, y0)), hash2(vec2(x1, y0)), w.x),
               mix(hash2(vec2(x0, y1)), hash2(vec2(x1, y1)), w.x), w.y);
}

// Octaves double the frequency AND the period together, so every octave stays
// closed at the seam.
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

    // ── 1. ORGANIC WAVEFRONT CONTOUR & MULTI-FREQUENCY TURBULENCE ─────────────
    // Multi-scale aerodynamic turbulence gives an organic, living shockwave contour
    // that expands with explosive power without looking like a synthetic CAD circle.
    float frontWob  = FbmRing(vec2(u * 5.0, 7.0 + u_seed), 5.0, 2) - 0.5;
    float frontRip  = FbmRing(vec2(u * 16.0, 31.0 + u_seed), 16.0, 2) - 0.5;
    float frontFine = FbmRing(vec2(u * 48.0, 83.0 + u_seed), 48.0, 2) - 0.5;

    // Organic wavefront location with natural radial wander
    float coreV = u_coreV + frontWob * 0.06;
    float frontV = coreV + 0.03 + (frontWob * 0.04 + frontRip * 0.028 + frontFine * 0.012);

    // ── 2. VARIABLE ARC ENERGY & INTENSITY MODULATION ────────────────────────
    // Explosions release energy unevenly around their perimeter: some arcs
    // burn fiercely with intense plasma, while others are thinner and cooler.
    float arcNoise = FbmRing(vec2(u * 4.0, 119.0 + u_seed), 4.0, 2);
    float arcEnergy = mix(0.55, 1.40, smoothstep(0.15, 0.85, arcNoise));

    // Dynamic leading edge thickness that breathes naturally around the circumference
    float rimT = FbmRing(vec2(u * 8.0, 101.0 + u_seed), 8.0, 2);
    float frontWidth = mix(0.012, 0.032, rimT) * mix(1.0, 0.65, u_t01);

    // ── 3. RAZOR-SHARP SUPERSONIC LEADING EDGE ──────────────────────────────
    float dR = v - frontV; // > 0: ahead of shock (outside), < 0: behind shock (inside wake)

    // Razor-sharp outer falloff (strictly 0 ahead of the shockwave)
    float leadEdge = 0.0;
    if (dR >= 0.0) {
        leadEdge = exp(-dR / (frontWidth * 0.25)) * (1.0 - smoothstep(0.01, 0.035, dR));
    } else {
        leadEdge = exp(dR / (frontWidth * 1.6));
    }

    // Ultra-fine white-hot core line with arc modulation
    float coreLine = exp(-abs(dR) * 220.0) * arcEnergy;

    // ── 4. IRREGULAR TRAILING PLASMA WAKE & JET TENDRILS ─────────────────────
    // Trailing wake ONLY exists behind the shockfront (dR <= 0.0). Ahead of it is empty air.
    float wake = 0.0;
    float rays = 0.0;
    float wd = max(-dR, 0.0); // distance trailing behind front

    if (dR <= 0.0)
    {
        // Tangential vortex shear slightly curls trailing wisps
        float shear = (wd * 0.015 + wd * wd * 0.03) * (FbmRing(vec2(u * 4.0, 41.0 + u_seed), 4.0, 2) - 0.5);
        float uw = u + shear;

        // Directional radial speedline rays
        float ray1 = ShockNoise(vec2(uw * 36.0, 17.0 + u_seed), 36.0);
        float ray2 = ShockNoise(vec2(uw * 84.0, 59.0 + u_seed), 84.0);
        float ray3 = ShockNoise(vec2(uw * 168.0, 113.0 + u_seed), 168.0);
        float rayComb = pow(ray1 * 0.45 + ray2 * 0.35 + ray3 * 0.20, 2.0) * 2.8;

        // Irregular tail reach: some tendrils stretch deep inward, others stay compact
        float tailReach = FbmRing(vec2(u * 7.0, 203.0 + u_seed), 7.0, 2);
        float rayLen = mix(0.12, 0.28, u_t01) * mix(0.45, 1.55, tailReach);
        float rayDecay = exp(-wd / max(rayLen * 0.40, 0.02));
        rays = rayComb * rayDecay * arcEnergy;

        // Aerodynamic plasma body trailing behind the front
        float wakeBody = exp(-wd * 20.0) * (1.0 - smoothstep(0.0, max(rayLen, 0.15), wd));

        // Concentric acoustic pressure ripple
        float pressureRipple = (sin(wd * 85.0 - u_t01 * 10.0) * 0.5 + 0.5) * exp(-wd * 26.0);

        // Organic shredding & arc tear as shockwave expands
        float shredNoise = ShockNoise(vec2(uw * 48.0, wd * 14.0 + 77.0 + u_seed), 48.0);
        float tearThreshold = mix(0.06, 0.55, u_t01 * u_t01) + (1.0 - arcEnergy * 0.7) * 0.15;
        float shredMask = smoothstep(tearThreshold, tearThreshold + 0.22, shredNoise);

        wake = clamp((wakeBody * 0.85 + rays * 0.65 + pressureRipple * 0.25) * shredMask, 0.0, 1.0);
    }

    // Hot filament sparks & ember nodes
    float ember = pow(rays * 0.65 + leadEdge * 0.35, 2.0) * mix(1.2, 0.3, u_t01) * arcEnergy;

    // Life falloff (fast arrival, smooth organic dissipation)
    float lifeAlpha = pow(max(1.0 - u_t01, 0.0), 1.8);
    float frontIntensity = (leadEdge * 1.5 + coreLine * 2.0) * lifeAlpha * arcEnergy;
    float wakeIntensity = wake * lifeAlpha;

    // Canvas boundary & inner hole protection (strictly 0 at mesh edges)
    float edge = smoothstep(0.0, 0.04, v) * (1.0 - smoothstep(0.94, 1.0, v));
    edge *= smoothstep(u_hole * 0.4, u_hole * 1.1, v);

    // ── 5. DUAL-PASS COMPOSITING (BRIGHT-BACKGROUND VFX CONTRACT) ────────────
    if (u_premultiply > 0.5)
    {
        // EMISSION PASS: Luminous bloom for razor-sharp wavefront + filaments
        float glowMask = clamp(frontIntensity * 3.6 + ember * 2.2 + wakeIntensity * 0.35, 0.0, 1.0) *
                         edge * u_opacity * u_bodyColor.a;
        float ga = clamp(frontIntensity * 0.70 + ember * 0.45 + wakeIntensity * 0.20, 0.0, 1.0) *
                   edge * u_opacity * u_bodyColor.a;

        // White-hot shock core line ramping to vivid element glow
        vec3 hotCol = mix(u_glowColor.rgb, vec3(1.0, 0.98, 0.94),
                          clamp(coreLine * 0.95 + leadEdge * 0.60 + ember * 0.30, 0.0, 1.0));

        finalColor = VFX_ResolvePremultiplied(hotCol, u_emission * 0.35, ga,
                                              hotCol, glowMask, u_emission);
    }
    else
    {
        // BODY PASS: Rich elemental plasma pigmentation
        float cover = clamp(frontIntensity * 0.95 + wakeIntensity * 1.10 + ember * 0.40, 0.0, 1.0) * edge;
        float heat = clamp(1.0 - wd * 4.0, 0.0, 1.0);
        
        vec3 deepPlasma = u_bodyColor.rgb * mix(0.45, 1.00, heat);
        vec3 bodyCol = mix(deepPlasma, u_glowColor.rgb,
                           clamp(leadEdge * 0.85 + ember * 0.65 + rays * 0.35, 0.0, 1.0));
        float a = cover * u_opacity * u_bodyColor.a;
        finalColor = VFX_ResolveBody(bodyCol, u_emission, a);
    }
}
