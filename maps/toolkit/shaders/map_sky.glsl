vec3 MapSkyRadiance(vec3 ray, vec3 sun, vec3 ambient, vec3 haze, vec3 direct)
{
    ray = normalize(ray);
    float daylight = smoothstep(-0.12, 0.08, sun.y);
    vec3 sky = mix(haze * ambient * 0.72, ambient * vec3(0.18, 0.38, 0.72),
                   smoothstep(0.0, 0.65, max(ray.y, 0.0)));
    float cosine = clamp(dot(ray, sun), 0.0, 1.0);
    float width = max(fwidth(cosine), 0.000002);
    float disc = smoothstep(0.999989 - width, 0.999989 + width, cosine);
    return mix(ambient * vec3(0.025, 0.035, 0.060), sky, daylight)
         + direct * (pow(cosine, 32.0) * 0.12 + disc * 4.0) * daylight;
}
