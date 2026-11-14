vec3 MapSkyRadiance(vec3 ray, vec3 sun, vec3 ambient, vec3 haze, vec3 direct)
{
    float rayLenSq = max(dot(ray, ray), 1e-8);
    ray *= inversesqrt(rayLenSq);

    float daylight = smoothstep(-0.12, 0.08, sun.y);
    float night = 1.0 - daylight;

    float zenithFactor = smoothstep(0.0, 0.65, max(ray.y, 0.0));
    vec3 horizonColor = haze * 0.78 + direct * 0.045;

    // Morning dawn warmth along the horizon facing the sun
    float horizLenSq = dot(ray.xz, ray.xz);
    float sunHorizLenSq = dot(sun.xz, sun.xz);
    if (horizLenSq > 1e-6 && sunHorizLenSq > 1e-6) {
        float sunAzimuth = dot(ray.xz, sun.xz) * inversesqrt(horizLenSq * sunHorizLenSq);
        vec3 dawnColor = direct * vec3(1.12, 0.72, 0.42);
        horizonColor = mix(horizonColor, dawnColor, smoothstep(-0.2, 0.85, sunAzimuth) * (1.0 - zenithFactor) * 0.45);
    }

    vec3 zenithColor  = ambient * vec3(0.35, 0.52, 0.82) + vec3(0.036, 0.048, 0.065);
    vec3 daySky = mix(horizonColor, zenithColor, zenithFactor);

    // Deep ethereal night sky (midnight navy at horizon, deep cosmos at zenith)
    vec3 nightHorizon = ambient * vec3(0.08, 0.12, 0.20) + vec3(0.008, 0.012, 0.025);
    vec3 nightZenith  = ambient * vec3(0.02, 0.03, 0.07) + vec3(0.002, 0.004, 0.012);
    vec3 nightSky = mix(nightHorizon, nightZenith, zenithFactor);

    // Night Celestial Features: Stars, Moon & Aurora Borealis (Cực quang)
    vec3 nightAdditions = vec3(0.0);
    if (night > 0.001 && ray.y > 0.02) {
        // 1. Procedural Twinkling Star Field (sin-less ALU hash, evaluated only above horizon haze)
        if (ray.y > 0.06) {
            vec3 starP = ray * 160.0;
            vec3 starId = floor(starP);
            vec3 h3 = fract(starId * vec3(0.1031, 0.1030, 0.0973));
            h3 += dot(h3, h3.yxz + 33.33);
            float starHash = fract((h3.x + h3.y) * h3.z);
            if (starHash > 0.982) {
                vec3 starFd = starP - starId - 0.5;
                float starDistSq = dot(starFd, starFd);
                if (starDistSq < 0.04) {
                    float starDist = starDistSq * inversesqrt(max(starDistSq, 1e-8));
                    float starBrightness = (1.0 - smoothstep(0.0, 0.20, starDist)) * ((starHash - 0.982) * 55.555556);
                    vec3 starTint = mix(vec3(0.85, 0.92, 1.0), vec3(1.0, 0.88, 0.72), fract(starHash * 17.3));
                    nightAdditions += starTint * (starBrightness * 1.8);
                }
            }
        }

        // 2. Moon & Lunar Halo (positioned opposite the sun vector)
        vec3 moonRaw = vec3(-sun.x, max(-sun.y, 0.35), -sun.z);
        float rawMoonDot = dot(ray, moonRaw);
        if (rawMoonDot > 0.0) {
            float moonCos = rawMoonDot * inversesqrt(max(dot(moonRaw, moonRaw), 1e-8));
            if (moonCos > 0.85) {
                float m2 = moonCos * moonCos;
                float m4 = m2 * m2;
                float m16 = m4 * m4;
                float m64 = m16 * m16;

                vec3 moonHalo = vec3(0.42, 0.55, 0.85) * (m16 * 0.08 + m64 * 0.25);
                float moonDisc = smoothstep(0.9991, 0.9995, moonCos);
                nightAdditions += vec3(0.92, 0.96, 1.0) * (moonDisc * 3.2) + moonHalo;
            }
        }

        // 3. Aurora Borealis (Cực quang: undulating emerald and violet curtains)
        float auroraMask = smoothstep(0.08, 0.40, ray.y) * (1.0 - smoothstep(0.20, 0.85, ray.y));
        if (auroraMask > 0.0) {
            float invRayY = 1.0 / max(ray.y, 0.08);
            float aX = ray.x * invRayY;
            float aZ = ray.z * invRayY;
            float curtainA = sin(aX * 3.8 + aZ * 1.8);
            float curtainB = cos(aX * 7.5 - aZ * 3.2);
            float auroraWave = smoothstep(0.25, 0.88, abs(curtainA + curtainB * 0.5) * 0.65);

            vec3 auroraGreen  = vec3(0.12, 0.88, 0.48);
            vec3 auroraViolet = vec3(0.62, 0.22, 0.85);
            vec3 auroraTint = mix(auroraGreen, auroraViolet, smoothstep(0.18, 0.55, ray.y));
            nightAdditions += auroraTint * (auroraWave * auroraMask * 0.48);
        }
    }

    // Day Sun & Solar Corona (skip anti-solar hemisphere where dot(ray, sun) <= 0)
    vec3 sunRadiance = vec3(0.0);
    if (daylight > 0.0001) {
        float cosine = dot(ray, sun);
        if (cosine > 0.0) {
            cosine = min(cosine, 1.0);
            float c2  = cosine * cosine;
            float c4  = c2 * c2;
            float c8  = c4 * c4;
            float c16 = c8 * c8;

            float disc = smoothstep(0.999983, 0.999995, cosine);
            float horizonGlow = (1.0 - zenithFactor) * c4 * 0.04;
            float warmHalo = c4 * 0.08 * (1.0 - zenithFactor * 0.5);
            vec3 haloTint = vec3(1.15, 0.95, 0.72);
            sunRadiance = direct * (horizonGlow + c16 * 0.18 + disc * 4.0) + direct * haloTint * warmHalo;
        }

        // 2-layer procedural sunset cirrus/cumulus clouds above horizon
        if (ray.y > 0.035) {
            float invY = 1.0 / max(ray.y, 0.06);
            vec2 cUV = ray.xz * invY * 0.42;
            float w1 = sin(cUV.x * 2.1 + cos(cUV.y * 1.7)) * cos(cUV.y * 1.9 - sin(cUV.x * 1.4));
            float w2 = sin(cUV.x * 4.7 - cUV.y * 3.8 + 1.2) * 0.5 + 0.5;
            float cloudShape = smoothstep(0.18, 0.72, (w1 * 0.5 + 0.5) * 0.65 + w2 * 0.35);
            float cloudHorizonFade = smoothstep(0.035, 0.18, ray.y) * (1.0 - smoothstep(0.45, 0.92, ray.y));
            float sunEdge = smoothstep(-0.3, 0.95, dot(ray, sun));
            vec3 cloudDark = mix(ambient * 0.75 + vec3(0.16, 0.14, 0.22), haze * 0.85, 0.45);
            vec3 cloudLit = direct * vec3(1.15, 0.88, 0.62);
            vec3 cloudCol = mix(cloudDark, cloudLit, 0.25 + 0.75 * sunEdge);
            daySky = mix(daySky, cloudCol, cloudShape * cloudHorizonFade * 0.42);
        }
    }

    return mix(nightSky + nightAdditions, daySky + sunRadiance, daylight);
}