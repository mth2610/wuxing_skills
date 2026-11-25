#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// ── VFX_ComposeShockRing: razor-sharp leading edge + torn Wuxia wake ────────
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
uniform float u_hole;  // inner fade threshold
uniform float u_premultiply; // 0 = BODY (BLEND_ALPHA), 1 = EMISSION (premultiplied)

// Variant style packs:
//   u_styleA = (frontWidthScale, frontHard, wobble, fadeK)
//   u_styleB = (wakeScale, raysScale, tear0, tear1)
//   u_styleC = (lensingAmp, 0=reserved, breakup, vapour)
//   u_styleD = (sootCover, coreLineScale, dir, bodyGain)
//   u_arc    = (centerU, halfSpanU)
uniform vec4  u_styleA;
uniform vec4  u_styleB;
uniform vec4  u_styleC;
uniform vec4  u_styleD;
uniform vec2  u_arc;

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

    // Screen-space AA floors in canvas & polar units (avoiding u=0/1 wrap spike).
    float pxV = max(fwidth(v), 0.0005);
    float pxU = max(min(fwidth(u), fwidth(fract(u + 0.5))), 0.00005);

    // Implosion flips the radial coordinate so the front leads inward.
    float vEff = (u_styleD.z < 0.0) ? (1.0 - v) : v;

    // ── 1. ORGANIC WAVEFRONT CONTOUR & ANGULAR FRACTURE ──────────────────────
    float wobbleAmp = u_styleA.z;
    float frontWob  = FbmRing(vec2(u * 4.0, 7.0 + u_seed), 4.0, 2) - 0.5;
    float frontRip  = FbmRing(vec2(u * 12.0, 31.0 + u_seed), 12.0, 2) - 0.5;
    float frontFine = FbmRing(vec2(u * 36.0, 83.0 + u_seed), 36.0, 2) - 0.5;

    // Sharp angular sawtooth/stepped kinks for SHATTER.
    float kink = 0.0;
    if (u_styleC.z > 0.01) {
        float kNoise = FbmRing(vec2(u * 7.0, 53.0 + u_seed), 7.0, 2);
        float tri = abs(fract(u * 14.0 + kNoise * 1.8) * 2.0 - 1.0) - 0.5;
        kink = u_styleC.z * (tri * 0.075 + (floor(kNoise * 4.0) / 4.0 - 0.5) * 0.065);
    }

    // Directional arc distance & tip taper/pull-back (crescent shape for partial arcs).
    float du = abs(fract(u - u_arc.x + 0.5) - 0.5);
    float arcFade = (u_arc.y >= 0.499) ? 1.0 : (1.0 - smoothstep(u_arc.y * 0.68, u_arc.y, du));
    float arcPull = (u_arc.y >= 0.499) ? 0.0 : smoothstep(u_arc.y * 0.60, u_arc.y, du) * 0.09;

    float coreV = u_coreV + frontWob * 0.09 * wobbleAmp;
    float frontV = coreV + 0.03 + (frontWob * 0.095 + frontRip * 0.048 + frontFine * 0.018) * wobbleAmp + kink - arcPull;

    // ── 2. VARIABLE ARC ENERGY & BREAKUP ─────────────────────────────────────
    float arcNoise = FbmRing(vec2(u * 3.0, 119.0 + u_seed), 3.0, 2);
    float arcEnergy = mix(0.28, 1.55, smoothstep(0.18, 0.82, arcNoise));

    float breakField = FbmRing(vec2(u * 6.0, 151.0 + u_seed), 6.0, 2);
    float arcBreak = (u_styleC.z > 0.001)
        ? smoothstep(u_styleC.z - 0.02, u_styleC.z + 0.06, breakField)
        : 1.0;
    arcEnergy *= arcBreak * arcFade;

    // Dynamic leading edge thickness with pixel-width AA floor.
    float rimT = FbmRing(vec2(u * 8.0, 101.0 + u_seed), 8.0, 2);
    float frontWidth = max(mix(0.012, 0.030, rimT) * mix(1.0, 0.65, u_t01) * max(u_styleA.x, 0.05),
                           1.4 * pxV);

    // ── 3. RAZOR-THIN LEADING EDGE & CORE LINE ───────────────────────────────
    float dR = vEff - frontV; // > 0: ahead of shock, < 0: behind shock (inside wake)
    float hard = clamp(u_styleA.y, 0.12, 3.0);

    float leadEdge = 0.0;
    if (dR >= 0.0) {
        leadEdge = exp(-dR / max(frontWidth * (0.28 / hard), pxV * 0.80)) *
                   (1.0 - smoothstep(pxV * 0.5, max(0.032 / hard, pxV * 1.8), dR));
    } else {
        leadEdge = exp(dR / max(frontWidth * (1.45 / clamp(hard, 0.5, 2.2)), pxV * 1.0));
    }

    // Ultra-thin hot core (kept narrow so bloom only hits the razor leading edge).
    float coreSigma = max(0.0036, pxV * 0.78);
    float coreLine = exp(-abs(dR) / coreSigma) * arcEnergy * u_styleD.y;

    // ── 4. CRISP DOMAIN-WARPED WAKE, JET RAYS & SONIC LENSING ────────────────
    float wake = 0.0;
    float rays = 0.0;
    float lensingDark = 0.0;
    float wd = max(-dR, 0.0);

    if (dR <= 0.0)
    {
        float shear = (wd * 0.018 + wd * wd * 0.035) *
                      (FbmRing(vec2(u * 4.0, 41.0 + u_seed), 4.0, 2) - 0.5) * wobbleAmp;
        float uw = u + shear;

        // Frequency-attenuated radial speedline rays (prevents sub-pixel aliasing).
        float f28  = 1.0 - smoothstep(0.25, 0.50, pxU * 28.0);
        float f64  = 1.0 - smoothstep(0.25, 0.50, pxU * 64.0);
        float f128 = 1.0 - smoothstep(0.25, 0.50, pxU * 128.0);

        float ray1 = ShockNoise(vec2(uw * 28.0, 17.0 + u_seed), 28.0);
        float ray2 = ShockNoise(vec2(uw * 64.0, 59.0 + u_seed), 64.0);
        float ray3 = ShockNoise(vec2(uw * 128.0, 113.0 + u_seed), 128.0);
        float rayComb = pow(ray1 * 0.50 * f28 + ray2 * 0.35 * f64 + ray3 * 0.15 * f128, 2.3) * 3.4;

        float tailReach = FbmRing(vec2(u * 6.0, 203.0 + u_seed), 6.0, 2);
        float rayLen = mix(0.16, 0.34, u_t01) * mix(0.45, 1.60, tailReach);
        float rayDecay = exp(-wd / max(rayLen * 0.48, 0.025));
        rays = rayComb * rayDecay * arcEnergy * u_styleB.y;

        // Domain-warped wake body so inner smoke/plasma forms crisp torn wisps.
        float wdWarp = wd + (FbmRing(vec2(u * 9.0, 29.0 + u_seed), 9.0, 2) - 0.5) * 0.075 * max(wobbleAmp, 0.35);
        wdWarp = max(wdWarp, 0.0);
        float wakeBody = exp(-wdWarp * 9.5) * (1.0 - smoothstep(0.0, max(rayLen * 1.15, 0.22), wdWarp)) * u_styleB.x;

        // Acoustic pressure ripple gated past early release.
        float rippleGate = smoothstep(0.08, 0.20, u_t01);
        float pressureRipple = (sin(wd * 65.0 - u_t01 * 10.0) * 0.5 + 0.5) * exp(-wd * 22.0) * rippleGate;

        float shredNoise = ShockNoise(vec2(uw * 40.0, wdWarp * 12.0 + 77.0 + u_seed), 40.0);
        float tearThreshold = mix(u_styleB.z, u_styleB.w, u_t01 * u_t01) + (1.0 - arcEnergy * 0.65) * 0.14;
        float shredMask = smoothstep(tearThreshold, tearThreshold + 0.10, shredNoise);

        wake = clamp((wakeBody * 0.90 + rays * 0.78 + pressureRipple * 0.18 * u_styleB.x) * shredMask, 0.0, 1.0);

        // Dark pressure-drop lensing trough immediately behind SONIC front.
        if (u_styleC.x > 0.01) {
            float rw = max(0.022, 1.8 * pxV);
            lensingDark = exp(-pow((wd - 0.055) / rw, 2.0)) * u_styleC.x;
        }
    }

    float ember = pow(rays * 0.70 + leadEdge * 0.25, 2.2) * mix(1.1, 0.25, u_t01) * arcEnergy * clamp(u_styleB.y, 0.0, 1.5);

    // Single life envelope (fadeK lives here; C side supplies arrival only).
    float lifeAlpha = pow(max(1.0 - u_t01, 0.0), max(u_styleA.w, 0.1));
    float frontIntensity = (leadEdge * 1.15 + coreLine * 1.55) * lifeAlpha * arcEnergy;
    float wakeIntensity = wake * lifeAlpha * arcFade;
    lensingDark *= lifeAlpha * arcFade;

    // Soft canvas boundary & feathered inner fade (no harsh center circle cutout).
    float edge = smoothstep(0.0, 0.05, v) * (1.0 - smoothstep(0.92, 1.0, v));
    edge *= smoothstep(u_hole * 0.45, max(u_hole + 0.16, 0.04), vEff);

    // Vapour desaturation for SONIC / PARRY / PULSE.
    vec3 vapourTint = vec3(0.86, 0.91, 0.96);
    vec3 baseBody = mix(u_bodyColor.rgb, vapourTint, clamp(u_styleC.w, 0.0, 1.0));
    vec3 baseGlow = mix(u_glowColor.rgb, vapourTint, clamp(u_styleC.w * 0.80, 0.0, 1.0));

    // ── 5. DUAL-PASS COMPOSITING (BRIGHT-BACKGROUND VFX CONTRACT) ────────────
    if (u_premultiply > 0.5)
    {
        // Keep bloom strictly on the razor coreLine & hottest filament tips so
        // the wake wisps, speedlines, and lensing trough stay crisp!
        float glowMask = clamp(coreLine * 1.25 * lifeAlpha + ember * 0.95 * lifeAlpha + leadEdge * 0.35 * lifeAlpha * arcEnergy,
                               0.0, 1.0) * edge * u_opacity * u_bodyColor.a;
        float ga = clamp(coreLine * 0.55 * lifeAlpha + leadEdge * 0.38 * lifeAlpha * arcEnergy + ember * 0.30 * lifeAlpha,
                         0.0, 1.0) * edge * u_opacity * u_bodyColor.a;

        vec3 hotCol = mix(baseGlow, vec3(1.0, 0.98, 0.94),
                          clamp(coreLine * 0.90 + ember * 0.30, 0.0, 1.0));
        vec3 glow = VFX_TonemapSafeEmission(hotCol, u_emission, glowMask);
        vec3 bodyPre = VFX_TonemapSafeEmission(hotCol, u_emission * 0.22, 1.0);

        finalColor = VFX_ResolvePremultiplied(bodyPre, 1.0, ga, glow, 1.0, 1.0);
    }
    else
    {
        // BODY PASS: Rich elemental plasma + soot pigmentation for bright-bg attenuation.
        float sootMask = clamp(wakeIntensity * 1.35 + leadEdge * 0.40 * arcEnergy + lensingDark * 1.15, 0.0, 1.0) *
                         u_styleD.x * edge;
        float cover = clamp((frontIntensity * 0.72 + wakeIntensity * 1.15 + ember * 0.35 + lensingDark * 0.90) * u_styleD.w + sootMask,
                            0.0, 1.0) * edge;
        float heat = clamp(1.0 - wd * 3.5, 0.0, 1.0);

        vec3 sootCol = baseBody * 0.22;
        vec3 deepPlasma = mix(baseBody * mix(0.45, 1.00, heat), sootCol,
                              clamp(sootMask * 0.88 + lensingDark * 0.95, 0.0, 1.0));
        vec3 bodyCol = mix(deepPlasma, baseGlow,
                           clamp((leadEdge * 0.70 + ember * 0.55 + rays * 0.35) * (1.0 - lensingDark * 0.92), 0.0, 1.0));
        float a = clamp(cover * u_opacity * u_bodyColor.a, 0.0, 1.0);
        finalColor = VFX_ResolveBody(bodyCol, min(u_emission, 1.2), a);
    }
}
