vec3 MapSkyRadiance(vec3 ray, vec3 sun, vec3 ambient, vec3 haze, vec3 direct)
{
    ray = normalize(ray);
    float daylight = smoothstep(-0.12, 0.08, sun.y);
    float night = 1.0 - daylight;

    float zenithFactor = smoothstep(0.0, 0.65, max(ray.y, 0.0));
    vec3 horizonColor = haze * 0.78 + direct * 0.045;

    // Morning dawn warmth along the horizon facing the sun
    float horizLen = length(ray.xz);
    if (horizLen > 0.001) {
        float sunAzimuth = dot(ray.xz / horizLen, normalize(sun.xz));
        vec3 dawnColor = direct * vec3(1.12, 0.72, 0.42);
        horizonColor = mix(horizonColor, dawnColor, smoothstep(-0.2, 0.85, sunAzimuth) * (1.0 - zenithFactor) * 0.45);
    }

    vec3 zenithColor  = ambient * vec3(0.30, 0.50, 0.90) + vec3(0.035, 0.060, 0.10);
    vec3 daySky = mix(horizonColor, zenithColor, zenithFactor);

    // Deep ethereal night sky (midnight navy at horizon, deep cosmos at zenith)
    vec3 nightHorizon = ambient * vec3(0.08, 0.12, 0.20) + vec3(0.008, 0.012, 0.025);
    vec3 nightZenith  = ambient * vec3(0.02, 0.03, 0.07) + vec3(0.002, 0.004, 0.012);
    vec3 nightSky = mix(nightHorizon, nightZenith, zenithFactor);

    // Night Celestial Features: Stars, Moon & Aurora Borealis (Cực quang)
    vec3 nightAdditions = vec3(0.0);
    if (night > 0.001 && ray.y > 0.02) {
        // 1. Procedural Twinkling Star Field
        float starGrid = 160.0;
        vec3 starP = ray * starGrid;
        vec3 starId = floor(starP);
        vec3 starFd = fract(starP) - 0.5;
        float starHash = fract(sin(dot(starId, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        if (starHash > 0.982 && ray.y > 0.06) {
            float starDist = length(starFd);
            float starBrightness = smoothstep(0.20, 0.0, starDist) * ((starHash - 0.982) / 0.018);
            vec3 starTint = mix(vec3(0.85, 0.92, 1.0), vec3(1.0, 0.88, 0.72), fract(starHash * 17.3));
            nightAdditions += starTint * starBrightness * 1.8;
        }

        // 2. Moon & Lunar Halo (positioned opposite the sun vector)
        vec3 moonDir = normalize(vec3(-sun.x, max(-sun.y, 0.35), -sun.z));
        float moonCos = clamp(dot(ray, moonDir), 0.0, 1.0);
        if (moonCos > 0.85) {
            float m2 = moonCos * moonCos;
            float m4 = m2 * m2;
            float m16 = m4 * m4;
            float m64 = m16 * m16;
            
            // Silver lunar halo / corona
            vec3 moonHalo = vec3(0.42, 0.55, 0.85) * (m16 * 0.08 + m64 * 0.25);
            // Sharp lunar disc
            float moonDisc = smoothstep(0.9991, 0.9995, moonCos);
            nightAdditions += vec3(0.92, 0.96, 1.0) * (moonDisc * 3.2) + moonHalo;
        }

        // 3. Aurora Borealis (Cực quang: undulating emerald and violet curtains)
        float auroraMask = smoothstep(0.08, 0.40, ray.y) * smoothstep(0.85, 0.20, ray.y);
        if (auroraMask > 0.0) {
            float aX = ray.x / max(ray.y, 0.08);
            float aZ = ray.z / max(ray.y, 0.08);
            float curtainA = sin(aX * 3.8 + aZ * 1.8);
            float curtainB = cos(aX * 7.5 - aZ * 3.2);
            float auroraWave = smoothstep(0.25, 0.88, abs(curtainA + curtainB * 0.5) * 0.65);
            
            vec3 auroraGreen  = vec3(0.12, 0.88, 0.48);
            vec3 auroraViolet = vec3(0.62, 0.22, 0.85);
            vec3 auroraTint = mix(auroraGreen, auroraViolet, smoothstep(0.18, 0.55, ray.y));
            nightAdditions += auroraTint * auroraWave * auroraMask * 0.48;
        }
    }

    // Day Sun & Solar Corona
    vec3 sunRadiance = vec3(0.0);
    if (daylight > 0.0001) {
        float cosine = clamp(dot(ray, sun), 0.0, 1.0);
        float c2  = cosine * cosine;
        float c4  = c2 * c2;
        float c8  = c4 * c4;
        float c16 = c8 * c8;

        float disc = 0.0;
        if (cosine > 0.9995) {
            float width = max(fwidth(cosine), 0.000002);
            disc = smoothstep(0.999989 - width, 0.999989 + width, cosine);
        }

        float horizonGlow = (1.0 - zenithFactor) * c4 * 0.04;
        sunRadiance = direct * (horizonGlow + c16 * 0.16 + disc * 4.0);
    }

    vec3 sky = mix(nightSky + nightAdditions, daySky + sunRadiance, daylight);
    return sky;
}