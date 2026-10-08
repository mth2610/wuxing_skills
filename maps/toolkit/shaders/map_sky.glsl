vec3 MapSkyRadiance(vec3 ray, vec3 sun, vec3 ambient, vec3 haze, vec3 direct)
{
    ray = normalize(ray);
    float daylight = smoothstep(-0.12, 0.08, sun.y);
    // Horizon radiance is scattered sunlight, independent of shaded sky fill.
    // Multiplying these together made a low-sun sky gray when shadows darkened.
    vec3 sky = mix(haze * 0.78 + direct * 0.045,
                   ambient * vec3(0.30, 0.50, 0.90) + vec3(0.035, 0.060, 0.10),
                   smoothstep(0.0, 0.65, max(ray.y, 0.0)));
    float cosine = clamp(dot(ray, sun), 0.0, 1.0);
    float width = max(fwidth(cosine), 0.000002);
    float disc = smoothstep(0.999989 - width, 0.999989 + width, cosine);
    return mix(ambient * vec3(0.025, 0.035, 0.060), sky, daylight)
         + direct * (pow(cosine, 16.0) * 0.16 + disc * 4.0) * daylight;
}
