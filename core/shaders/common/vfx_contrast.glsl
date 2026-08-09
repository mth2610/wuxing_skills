// Shared particle-stage Mass -> Structure -> Accent resolver. `params` is
// (enabled, edgeSharpness, coreSize, coreIntensity). It has no sampler, so it
// is safe in every VFX shader and adds no Vulkan descriptor binding.
vec4 VFXContrast_ResolveBody(vec4 source, float shapeAlpha, vec4 params)
{
    if (params.x < 0.5) return source;
    float shape = clamp(shapeAlpha, 0.0, 1.0);
    float structure = pow(shape, max(params.y, 0.01));
    // Retain a readable body while letting the thin detail ring fall away.
    source.rgb *= mix(0.62, 1.0, structure);
    // `source.a` also contains the per-particle/lifetime coverage. Never use
    // it as the shape signal: a dim mass particle would otherwise never reach
    // its own core threshold. Replace only the texture-alpha factor.
    source.a *= structure / max(shape, 0.001);
    return source;
}

float VFXContrast_CoreMask(float shapeAlpha, vec4 params)
{
    // Profiles disabled: preserve the legacy full-sprite emissive boost.
    if (params.x < 0.5) return 1.0;
    // Smoke/dust deliberately have no Accent/Core. `smoothstep(1, 1, x)` is
    // undefined at coreSize=0 and on some drivers resolves as a full mask,
    // which then multiplied their unlit colour by coreIntensity=0 (black).
    if (params.z <= 0.0001 || params.w <= 0.0001) return 0.0;
    float structure = pow(clamp(shapeAlpha, 0.0, 1.0), max(params.y, 0.01));
    return smoothstep(clamp(1.0 - params.z, 0.0, 0.98), 1.0, structure);
}
