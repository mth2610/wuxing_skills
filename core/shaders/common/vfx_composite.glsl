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

vec4 VFX_ResolveBody(vec3 bodyColor, float bodyIntensity, float coverage)
{
    float a = clamp(coverage, 0.0, 1.0);
    return vec4(VFX_Finite3(bodyColor * max(bodyIntensity, 0.0)), a);
}

vec4 VFX_ResolvePremultiplied(vec3 bodyColor, float bodyIntensity,
                              float coverage, vec3 emissionColor,
                              float coreMask, float emissionGain)
{
    float a = clamp(VFX_Finite(coverage), 0.0, 1.0);
    vec3 body = bodyColor * max(bodyIntensity, 0.0) * a;
    vec3 glow = emissionColor * max(coreMask, 0.0) * max(emissionGain, 0.0);
    return vec4(VFX_Finite3(body + glow), a);
}

vec4 VFX_ResolveEmission(vec3 emissionColor, float gain, float mask,
                          float authoredAlpha)
{
    return vec4(VFX_Finite3(emissionColor * max(gain, 0.0) * max(mask, 0.0)),
                clamp(authoredAlpha, 0.0, 1.0));
}
