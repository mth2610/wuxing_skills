vec3 MapSkyRadiance(vec3 ray, vec3 sun, vec3 ambient, vec3 haze, vec3 direct)
{
    ray = normalize(ray);
    float daylight = smoothstep(-0.12, 0.08, sun.y);
    vec3 nightSky = ambient * vec3(0.025, 0.035, 0.060);

    // Fast out at night: skip sun disc derivatives and Mie scattering math
    if (daylight <= 0.0001)
        return nightSky;

    float zenithFactor = smoothstep(0.0, 0.65, max(ray.y, 0.0));
    vec3 horizonColor = haze * 0.78 + direct * 0.045;
    vec3 zenithColor  = ambient * vec3(0.30, 0.50, 0.90) + vec3(0.035, 0.060, 0.10);
    vec3 sky = mix(horizonColor, zenithColor, zenithFactor);

    float cosine = clamp(dot(ray, sun), 0.0, 1.0);
    // Multiply chain for pow(cosine, 4) and pow(cosine, 16) — zero pow() calls
    float c2  = cosine * cosine;
    float c4  = c2 * c2;
    float c8  = c4 * c4;
    float c16 = c8 * c8;

    // Anti-aliased solar disc (only evaluated near the sun cone to save fwidth stability)
    float disc = 0.0;
    if (cosine > 0.9995) {
        float width = max(fwidth(cosine), 0.000002);
        disc = smoothstep(0.999989 - width, 0.999989 + width, cosine);
    }

    // Visual upgrade: Soft wide atmospheric Mie glow (c4) + tight solar corona (c16)
    float horizonGlow = (1.0 - zenithFactor) * c4 * 0.04;
    vec3 sunRadiance = direct * (horizonGlow + c16 * 0.16 + disc * 4.0);

    return mix(nightSky, sky, daylight) + sunRadiance * daylight;
}