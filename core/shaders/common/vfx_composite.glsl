// Shared linear-space VFX output resolver.
// Producers provide coverage and emission separately; consumers choose the
// matching blend state (BLEND_ALPHA_PREMULTIPLY for ResolvePremultiplied,
// BLEND_ALPHA for ResolveBody, BLEND_ADDITIVE for ResolveEmission).
vec4 VFX_ResolveBody(vec3 bodyColor, float bodyIntensity, float coverage)
{
    float a = clamp(coverage, 0.0, 1.0);
    return vec4(bodyColor * max(bodyIntensity, 0.0), a);
}

vec4 VFX_ResolvePremultiplied(vec3 bodyColor, float bodyIntensity,
                              float coverage, vec3 emissionColor,
                              float coreMask, float emissionGain)
{
    float a = clamp(coverage, 0.0, 1.0);
    vec3 body = bodyColor * max(bodyIntensity, 0.0) * a;
    vec3 glow = emissionColor * max(coreMask, 0.0) * max(emissionGain, 0.0);
    return vec4(body + glow, a);
}

vec4 VFX_ResolveEmission(vec3 emissionColor, float gain, float mask,
                          float authoredAlpha)
{
    return vec4(emissionColor * max(gain, 0.0) * max(mask, 0.0),
                clamp(authoredAlpha, 0.0, 1.0));
}
