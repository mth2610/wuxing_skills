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
    // At close range (< 14m), blades retain 100% of their 3D transverse-rounded cylinder normals
    // for rich directional sunlight contrast and sculptural depth.
    // As distance increases (14m -> 42m), smoothly interpolate toward terrain normal (0, 1, 0)
    // to eliminate distant specular noise, aliasing, and crawling sparkle.
    float distToCam = length(u_viewPos - worldPosition);
    float antiShimmer = smoothstep(14.0, 42.0, distToCam) * (1.0 - bloomMask);
    vec3 terrainNormal = vec3(0.0, 1.0, 0.0);
    float flattenFactor = mix(0.0, 0.85, antiShimmer);
    vec3 n = normalize(mix(faceNormal, terrainNormal, flattenFactor));
    // Gentle soil root transition: blend toward terrain normal near ground level
    n = normalize(mix(n, terrainNormal, (1.0 - smoothstep(0.0, 0.25, heightAlongPlant)) * 0.35));

    // Ghost of Tsushima Wrapped Diffuse Lighting:
    // I_diffuse = ((N · L + w) / (1 + w))^2
    // Quadratic falloff wraps light gently around curved foliage without harsh terminators
    float wrap = mix(0.32, 0.14, bloomMask);
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

    // Full Dynamic + Static Shadow Reception:
    // Receives real-time shadows from sun, characters, vegetation, rocks and self-shadowing
    float shadow = MapShadowVisibility(worldPosition, n, u_lightDir);

    // Intra-Canopy Self-Shadowing (Exponential Canopy Extinction):
    // Direct sunlight is attenuated as it penetrates down through the grass canopy.
    // Tips catch 100% unobstructed sun; middle & lower parts receive soft dappled light.
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float canopyExtinction = clamp(exp(-2.2 * (1.0 - hNorm)), 0.42, 1.0);

    // Hemispheric Ambient Lighting:
    // Upper surface catches cool sky ambient; lower surface catches warm chlorophyll grass bounce
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = max(u_ambientColor, vec3(0.25, 0.28, 0.22)) * vec3(1.04, 1.08, 1.16);
    vec3 groundBounce = max(u_ambientColor, vec3(0.25, 0.28, 0.22)) * vec3(0.50, 0.54, 0.32);
    float cupCavity = mix(0.55, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.72 + 0.28 * max(n.y, 0.0);
    float ambientVisibility = mix(0.92, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;

    // Direct sun lighting scaled by dynamic shadow and intra-canopy extinction
    vec3 direct = u_lightColor * directDiffuse * shadow * canopyExtinction;
    lit += baseColor * direct * 1.05;

    // Subsurface transmission glow (shines radiantly on backlit blades and petals)
    lit += subsurfaceColor * u_lightColor * transmission * mix(0.65, 0.88, bloomMask) * canopyExtinction;

    // Anisotropic Specular Glint along curved blade spine:
    // Transverse curvature gives crisp, sparkling highlight along the longitudinal fibers
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specPower = mix(32.0, 16.0, bloomMask);
    float specIntensity = mix(0.68 * (1.0 - antiShimmer * 0.75), 0.40, bloomMask);
    float spec = pow(NdotH, specPower) * specIntensity;
    lit += u_lightColor * spec * (0.35 + 0.65 * shadow) * canopyExtinction;

    // Soft Fresnel rim lighting along curved edges
    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.8) * mix(0.20, 0.28, bloomMask);
    lit += baseColor * u_lightColor * rim * canopyExtinction;

    // Root Contact AO: grounds foliage naturally into the soil without harsh pitch-black ink spots
    float rootAO = clamp(0.72 + 0.28 * (hNorm * (2.0 - hNorm)), 0.72, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
