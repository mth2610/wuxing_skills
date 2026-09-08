uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant, float uTransverse)
{
    vec3 viewDir = normalize(u_viewPos - worldPosition);
    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;
    // Ensure two-sided foliage always faces into upper hemisphere for sky ambient & sun reception
    if (faceNormal.y < 0.12) {
        faceNormal.y = abs(faceNormal.y) + 0.22;
        faceNormal = normalize(faceNormal);
    }

    // Distinguish upper bloom/petals (height > 0.65) from lower stems and grass blades
    float bloomMask = smoothstep(0.65, 0.90, heightAlongPlant);

    // Ghost of Tsushima: Curved Normals & Cylindrical Volume Illusion
    // N_curved = normalize(N_surface + u * S * k_curve)
    // uTransverse is in [-1.0, 1.0] across blade width (0.0 at spine/tip).
    // Spreads specular highlight smoothly across the blade like a 3D cylindrical reed.
    vec3 bladeSide = cross(faceNormal, vec3(0.0, 1.0, 0.0));
    if (length(bladeSide) > 0.001) {
        bladeSide = normalize(bladeSide);
        float u = clamp(uTransverse, -1.0, 1.0);
        faceNormal = normalize(faceNormal + bladeSide * (u * 0.60 * (1.0 - bloomMask)));
    }

    // Ghost of Tsushima: Distant Normal Flattening (Anti-Shimmering)
    // As distance increases, smoothly interpolate blade normals toward the terrain normal (0, 1, 0)
    // to eliminate high-frequency specular noise, aliasing, and crawling sparkle.
    float distToCam = length(u_viewPos - worldPosition);
    float antiShimmer = smoothstep(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    vec3 terrainNormal = vec3(0.0, 1.0, 0.0);
    // Near: individual 3D curved blades; Far: soft velvety rolling lawn carpet
    vec3 n = normalize(mix(faceNormal, terrainNormal, mix(0.48, 0.92, antiShimmer)));
    // Soil root transition: blend smoothly toward terrain normal near ground level
    n = normalize(mix(n, terrainNormal, (1.0 - smoothstep(0.0, 0.30, heightAlongPlant)) * 0.45));

    // Ghost of Tsushima Wrapped Diffuse Lighting:
    // I_diffuse = ((N · L + w) / (1 + w))^2
    // Quadratic falloff wraps light gently around curved foliage without harsh terminators
    float wrap = mix(0.35, 0.14, bloomMask);
    float wrapped = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = wrapped * wrapped;

    // Two-sided Subsurface Scattering (SSS) Transmission:
    // Light penetrates thin foliage membranes both via direct back-face sun illumination
    // and forward-scatter when looking toward the sun (Ghost of Tsushima / CryEngine foliage).
    float backfaceSun = max(-dot(faceNormal, u_lightDir), 0.0);
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = pow(backfaceSun, 1.6) * 0.70 + pow(viewSunAlign, 2.2) * 0.50;
    float thinness = mix(0.18, 1.0, smoothstep(0.06, 0.65, heightAlongPlant));
    float transmission = transmissionAngle * thinness;

    vec3 subsurfaceColor = mix(
        baseColor * vec3(1.38, 1.30, 0.55), // Grass: radiant emerald-gold backlit transmission
        baseColor * vec3(1.42, 1.22, 0.88) + vec3(0.08, 0.04, 0.02), // Petals: warm translucent glow
        bloomMask
    );

    float horizon = 0.72 + 0.28 * max(n.y, 0.0);
    float staticSlope = 1.0 - max(dot(n, normalize(u_lightDir)), 0.0);
    float shadow = MapStaticShadowVisibility(worldPosition, staticSlope);

    // Flower cup cavity ambient occlusion: soft self-shadowing inside the flower center
    float cupCavity = mix(0.55, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = max(u_ambientColor, vec3(0.25, 0.28, 0.22)) * mix(1.0, cupCavity, bloomMask);

    vec3 direct = u_lightColor * directDiffuse * shadow;
    float ambientVisibility = mix(0.92, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;
    lit += baseColor * direct * 0.95;

    // Subsurface transmission glow (shines in direct light and soft ambient)
    lit += subsurfaceColor * u_lightColor * transmission * mix(0.58, 0.85, bloomMask);

    // Crisp specular gleam along curved blade spine and edges (matches reference photo)
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specPower = mix(24.0, 14.0, bloomMask);
    float specIntensity = mix(0.55 * (1.0 - antiShimmer * 0.70), 0.38, bloomMask);
    float spec = pow(NdotH, specPower) * specIntensity;
    lit += u_lightColor * spec * (0.35 + 0.65 * shadow);

    // Soft Fresnel rim lighting along curved edges
    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.8) * mix(0.18, 0.28, bloomMask);
    lit += baseColor * u_lightColor * rim;

    // Root Contact AO: grounds foliage naturally into the soil without harsh pitch-black ink spots
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float rootAO = clamp(0.72 + 0.28 * (hNorm * (2.0 - hNorm)), 0.72, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
