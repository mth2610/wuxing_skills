// Shared linear-space VFX output resolver.
// Producers provide coverage and emission separately; consumers choose the
// matching blend state (BLEND_ALPHA_PREMULTIPLY for ResolvePremultiplied,
// BLEND_ALPHA for ResolveBody, BLEND_ADDITIVE for ResolveEmission).
// One NaN or Inf in an HDR emission term does not stay local: it goes into the
// scene target, the bloom prefilter picks it up as the brightest sample in its 4x4
// cell, and the pyramid smears it across the frame. BRIGHT_BACKGROUND_VFX_SPEC.md
// 8.2 requires "no sample may be NaN/Inf"; this is where that is enforced, once,
// for every producer. `v == v` is false only for NaN - clamp() cannot be used for
// the test because its NaN behaviour is undefined.
float VFX_Finite(float v) { return (v == v) ? min(v, 65504.0) : 0.0; }
vec3 VFX_Finite3(vec3 v) { return vec3(VFX_Finite(v.x), VFX_Finite(v.y), VFX_Finite(v.z)); }

// Inverse of post_process.fs' unclamped ACES fit. This is deliberately kept
// private to the opt-in output resolver below: applying hue restoration after
// scene compositing cannot distinguish the emitter from a bright background
// and can therefore manufacture occlusion. Here the emitter is still isolated.
float VFX_AcesInverse(float y)
{
    y = clamp(y, 0.0, 0.9999);
    float a = y * 2.43 - 2.51;
    float b = y * 0.59 - 0.03;
    float c = y * 0.14;
    float discriminant = max(b * b - 4.0 * a * c, 0.0);
    return max((-b - sqrt(discriminant)) / (2.0 * a), 0.0);
}

float VFX_AcesScalar(float x)
{
    return clamp((x * (2.51 * x + 0.03)) /
                 (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 VFX_TonemapSafeHDR(vec3 hdr)
{
    hdr = VFX_Finite3(max(hdr, vec3(0.0)));
    float peak = max(hdr.r, max(hdr.g, hdr.b));
    if (peak <= 0.0) return vec3(0.0);

    // Tone-map the peak once, retain channel ratios, then analytically invert
    // each target channel. Because post_process.fs already applies HDR-adaptive
    // hue restoration above 1.0, keep sub-threshold carrier ramps linear so
    // internal density structure stays smooth, and blend calibrated residual
    // inverse-ACES compensation above 1.0 HDR.
    vec3 ratio = hdr / peak;
    ratio = mix(ratio, vec3(1.0), smoothstep(5.0, 12.0, peak));
    vec3 target = ratio * VFX_AcesScalar(peak);
    vec3 inverseSolved = VFX_Finite3(vec3(VFX_AcesInverse(target.r),
                                          VFX_AcesInverse(target.g),
                                          VFX_AcesInverse(target.b)));
    float excessWeight = smoothstep(1.0, 2.4, peak) * 0.45;
    return mix(hdr, inverseSolved, excessWeight);
}

vec3 VFX_TonemapSafeEmission(vec3 emissionColor, float gain, float mask)
{
    return VFX_TonemapSafeHDR(emissionColor * max(gain, 0.0) * max(mask, 0.0));
}

vec4 VFX_ResolveBody(vec3 bodyColor, float bodyIntensity, float coverage)
{
    float a = clamp(coverage, 0.0, 1.0);
    return vec4(VFX_Finite3(bodyColor * max(bodyIntensity, 0.0)), a);
}

// Quadratic-onset chroma-weighted luminous opacity for premultiplied VFX:
//   - On dark backgrounds (dst ~ 0), (1 - a) * dst ~ 0 so dark visuals stay airy.
//   - On bright snow/daylight backgrounds (dst ~ (0.90, 0.95, 1.12)), alpha'(0) = 0
//     and max(alpha') <= 0.463 guarantees strictly positive d(out)/dp > +0.42
//     everywhere: zero dirty grey/brown outer rings and zero mid-tone flattening,
//     while saturated HDR glow (p >= 1.0) still blocks snow's competing channels.
float VFX_LuminousOpacity(vec3 glow)
{
    float peak = max(glow.r, max(glow.g, glow.b));
    if (peak <= 1e-4) return 0.0;
    float minC = min(glow.r, min(glow.g, glow.b));
    float sat  = clamp((peak - minC) / peak, 0.0, 1.0);
    float p2   = peak * peak;
    return clamp(0.82 * mix(0.18, 1.0, sat) * (p2 / (1.32 + p2)), 0.0, 0.88);
}

vec4 VFX_ResolvePremultiplied(vec3 bodyColor, float bodyIntensity,
                              float coverage, vec3 emissionColor,
                              float coreMask, float emissionGain)
{
    float a = clamp(VFX_Finite(coverage), 0.0, 1.0);
    vec3 body = bodyColor * max(bodyIntensity, 0.0) * a;
    vec3 glow = emissionColor * max(coreMask, 0.0) * max(emissionGain, 0.0);
    a = max(a, VFX_LuminousOpacity(glow));
    return vec4(VFX_Finite3(body + glow), a);
}

vec4 VFX_ResolveEmission(vec3 emissionColor, float gain, float mask,
                          float authoredAlpha)
{
    return vec4(VFX_Finite3(emissionColor * max(gain, 0.0) * max(mask, 0.0)),
                clamp(authoredAlpha, 0.0, 1.0));
}

// ── The output permutation ───────────────────────────────────────────────────
// A producer whose consumer picks its blend at RUNTIME cannot know statically
// which resolver above it owes. Writing `if (u_additive)` inside the shader
// does not fix that: it still lets the consumer set one blend while the shader
// resolves for another, and a mismatched pair does not fail — the effect still
// draws, just composited wrong, with nothing to notice it by.
//
// So the choice is a DEFINE, and the consumer loads the variant that matches
// the blend it is about to set (VFXRender_OutputDefines in core/vfx_render.h
// hands out the block). The pairing becomes structural rather than a
// convention someone has to remember.
//
// Declared only when one of the three is defined, so that the 25+ shaders that
// legitimately call VFX_ResolveBody/Emission/Premultiplied directly — because
// their blend is fixed — keep compiling untouched. A shader that CALLS this
// without a define gets an undeclared-function error, which is the point.
#if defined(OUTPUT_BODY) || defined(OUTPUT_EMISSION) || defined(OUTPUT_PREMULTIPLIED)
vec4 VFX_ResolveOutput(vec3 bodyColor, float bodyIntensity, float coverage,
                       vec3 emissionColor, float coreMask, float emissionGain)
{
#if defined(OUTPUT_EMISSION)
#if defined(VFX_TONEMAP_SAFE_EMISSION)
    vec3 glow = VFX_TonemapSafeEmission(emissionColor, emissionGain, coreMask);
    return vec4(glow, clamp(coverage, 0.0, 1.0));
#else
    return VFX_ResolveEmission(emissionColor, emissionGain, coreMask, coverage);
#endif
#elif defined(OUTPUT_PREMULTIPLIED)
#if defined(VFX_TONEMAP_SAFE_EMISSION)
    float a = clamp(VFX_Finite(coverage), 0.0, 1.0);
    vec3 body = bodyColor * max(bodyIntensity, 0.0) * a;
    vec3 glow = emissionColor * max(coreMask, 0.0) * max(emissionGain, 0.0);
    a = max(a, VFX_LuminousOpacity(glow));
    return vec4(VFX_TonemapSafeHDR(body + glow), a);
#else
    return VFX_ResolvePremultiplied(bodyColor, bodyIntensity, coverage,
                                    emissionColor, coreMask, emissionGain);
#endif
#else
    return VFX_ResolveBody(bodyColor, bodyIntensity, coverage);
#endif
}
#endif
