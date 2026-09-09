uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

// Fast Foliage Shadow Reception:
// Foliage consists of millions of thin overlapping blades where full 9-tap PCF
// (36-72 texture fetches/pixel) causes catastrophic texture cache thrashing.
// A single bilinear depth test provides smooth, artifact-free penumbra at 1/18th the GPU cost.
float FoliageShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    float slope = 1.0 - max(dot(normalize(normal), normalize(lightDir)), 0.0);
    float dynamicShadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        vec4 lightSpace = u_lightVP * vec4(worldPos, 1.0);
        vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float compareDepth = projected.z - mix(0.00045, 0.0016, slope);
            float vis = MapShadowCompareBilinear(shadowMap, projected.xy, compareDepth, u_shadowTexel);
            float edgeFade = MapShadowCoverageFade(projected.xy);
            dynamicShadow = mix(1.0, vis, edgeFade);
        }
    }
    float staticShadow = 1.0;
    if (u_staticShadowEnabled > 0.5) {
        vec4 lightSpace = u_staticLightVP * vec4(worldPos, 1.0);
        vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float compareDepth = projected.z - mix(0.0012, 0.0032, slope);
            float vis = MapShadowCompareBilinear(staticShadowMap, projected.xy, compareDepth, u_staticShadowTexel);
            float edgeFade = MapShadowCoverageFade(projected.xy);
            staticShadow = mix(1.0, vis, edgeFade);
        }
    }
    return min(dynamicShadow, staticShadow);
}

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
    float transmissionAngle = pow(backfaceSun, 1.5) * 0.70 + pow(viewSunAlign, 2.0) * 0.50;
    float thinness = mix(0.20, 1.0, smoothstep(0.06, 0.65, heightAlongPlant));
    float transmission = transmissionAngle * thinness;

    // Subsurface scattering color: radiant emerald-gold transmission
    vec3 subsurfaceColor = mix(
        baseColor * vec3(1.48, 1.40, 0.62), // Grass: radiant emerald-gold backlit transmission
        baseColor * vec3(1.42, 1.22, 0.88) + vec3(0.08, 0.04, 0.02), // Petals: warm translucent glow
        bloomMask
    );

    // Fast foliage shadow lookup (1 bilinear tap, 4 depth fetches instead of 72)
    float shadow = FoliageShadowVisibility(worldPosition, n, u_lightDir);

    // Intra-Canopy Self-Shadowing:
    // Soft, dappled attenuation as light filters through foliage canopy
    // Tips catch 100% direct sun; lower canopy stays pleasantly verdant (never pitch black)
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float canopyExtinction = clamp(exp(-1.2 * (1.0 - hNorm)), 0.58, 1.0);

    // Hemispheric Ambient Lighting:
    // Upper surface catches cool sky ambient; lower surface catches warm chlorophyll grass bounce
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 skyAmbient = max(u_ambientColor, vec3(0.28, 0.32, 0.24)) * vec3(1.06, 1.10, 1.16);
    vec3 groundBounce = max(u_ambientColor, vec3(0.28, 0.32, 0.24)) * vec3(0.65, 0.74, 0.44);
    float cupCavity = mix(0.65, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    float ambientVisibility = mix(0.92, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;

    // Direct sun lighting scaled by dynamic shadow and intra-canopy extinction
    vec3 direct = u_lightColor * directDiffuse * shadow * canopyExtinction;
    lit += baseColor * direct * 1.10;

    // Subsurface transmission glow (shines radiantly on backlit blades and petals)
    lit += subsurfaceColor * u_lightColor * transmission * mix(0.75, 0.92, bloomMask) * canopyExtinction;

    // Anisotropic Specular Glint along curved blade spine:
    // Transverse curvature gives crisp, sparkling highlight along the longitudinal fibers
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specPower = mix(28.0, 16.0, bloomMask);
    float specIntensity = mix(0.75 * (1.0 - antiShimmer * 0.75), 0.45, bloomMask);
    float spec = pow(NdotH, specPower) * specIntensity;
    lit += u_lightColor * spec * (0.35 + 0.65 * shadow) * canopyExtinction;

    // Soft Fresnel rim lighting along curved edges
    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.8) * mix(0.22, 0.30, bloomMask);
    lit += baseColor * u_lightColor * rim * canopyExtinction;

    // Root Contact AO: grounds foliage naturally into the soil without harsh pitch-black ink spots
    float rootAO = clamp(0.74 + 0.26 * pow(hNorm, 0.70), 0.74, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
