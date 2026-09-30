uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

// Fast Foliage Shadow Reception:
// Foliage consists of millions of thin overlapping blades where full 9-tap PCF
// (36-72 texture fetches/pixel) causes catastrophic texture cache thrashing.
// One depth sample per cascade keeps the dense foliage path inexpensive;
// this binary comparison does not provide a filtered penumbra.
// Linear lightSpace coordinates are precomputed in the vertex shader for zero-cost per-pixel projection.
float FoliageShadowVisibility(vec4 lightSpace, vec4 staticLightSpace, vec3 normal, vec3 lightDir)
{
    float slope = 1.0 - max(dot(normal, lightDir), 0.0);
    float dynamicShadow = 1.0;
    if (u_shadowEnabled > 0.5) {
        vec3 projected = lightSpace.xyz / max(lightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float bias = clamp(0.0018 + slope * 0.0035, 0.0015, 0.0055);
            dynamicShadow = (projected.z <= texture(shadowMap, projected.xy).r + bias) ? 1.0 : 0.0;
        }
    }
    float staticShadow = 1.0;
    if (u_staticShadowEnabled > 0.5) {
        vec3 projected = staticLightSpace.xyz / max(staticLightSpace.w, 0.00001) * 0.5 + 0.5;
        if (projected.z > 0.0 && projected.z < 1.0 &&
            projected.x > 0.0 && projected.x < 1.0 &&
            projected.y > 0.0 && projected.y < 1.0) {
            float bias = clamp(0.0025 + slope * 0.0040, 0.0020, 0.0070);
            staticShadow = (projected.z <= texture(staticShadowMap, projected.xy).r + bias) ? 1.0 : 0.0;
        }
    }
    return min(dynamicShadow, staticShadow);
}

vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant, float uTransverse,
                 vec4 lightSpace, vec4 staticLightSpace)
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
    float groundBlend = (1.0 - smoothstep(0.0, 0.25, heightAlongPlant)) * 0.35;
    float totalTerrainBlend = 1.0 - (1.0 - flattenFactor) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, terrainNormal, totalTerrainBlend));

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

    // Fast foliage shadow lookup (linear lightSpace coordinates interpolated from vertex shader)
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir);

    // Intra-Canopy Self-Shadowing:
    // Soft, dappled attenuation as light filters through foliage canopy
    // Tips catch 100% direct sun; lower canopy stays pleasantly verdant (never pitch black)
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float canopyExtinction = clamp(exp(-1.2 * (1.0 - hNorm)), 0.58, 1.0);

    // Hemispheric Ambient Lighting:
    // Upper surface catches cool sky ambient; lower surface catches warm chlorophyll grass bounce
    float skyWeight = n.y * 0.5 + 0.5;
    vec3 baseAmbient = max(u_ambientColor, vec3(0.28, 0.32, 0.24));
    vec3 skyAmbient = baseAmbient * vec3(1.06, 1.10, 1.16);
    vec3 groundBounce = baseAmbient * vec3(0.65, 0.74, 0.44);
    float cupCavity = mix(0.65, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = mix(groundBounce, skyAmbient, skyWeight) * mix(1.0, cupCavity, bloomMask);

    float horizon = 0.75 + 0.25 * max(n.y, 0.0);
    float ambientVisibility = mix(0.92, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;

    // Specular and Rim terms
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float specPower = mix(28.0, 16.0, bloomMask);
    float specIntensity = mix(0.75 * (1.0 - antiShimmer * 0.75), 0.45, bloomMask);
    float spec = pow(NdotH, specPower) * specIntensity;

    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.8) * mix(0.22, 0.30, bloomMask);

    // Direct sun, SSS, specular, and rim consolidated under single sunScale
    vec3 sunScale = u_lightColor * canopyExtinction;
    vec3 sunTerms = baseColor * (directDiffuse * shadow * 1.10 + rim)
                  + subsurfaceColor * (transmission * mix(0.75, 0.92, bloomMask))
                  + vec3(spec * (0.35 + 0.65 * shadow));
    lit += sunTerms * sunScale;

    // Root Contact AO: grounds foliage naturally into the soil without harsh pitch-black ink spots
    float rootAO = clamp(0.74 + 0.26 * pow(hNorm, 0.70), 0.74, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}

// Opaque grass uses a broad lighting response; preserve silhouette contrast
// without amplifying a one-pixel leaf into a bright specular line.
vec3 GrassShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                float heightAlongPlant, float uTransverse,
                vec4 lightSpace, vec4 staticLightSpace)
{
    float h = clamp(heightAlongPlant, 0.0, 1.0);
    vec3 viewDir = normalize(u_viewPos - worldPosition);
    // Mesh normals already include the rounded blade and clump response.
    // Two-sided grass retains that upward canopy normal on either face.
    vec3 bladeNormal = normalize(worldNormal);
    vec3 faceNormal = vec3(bladeNormal.x, abs(bladeNormal.y), bladeNormal.z);
    float widthPixels = 2.0 / max(fwidth(uTransverse), 0.0001);
    float unresolved = 1.0 - smoothstep(1.0, 3.0, widthPixels);
    float distToCam = length(u_viewPos - worldPosition);
    float antiShimmer = max(unresolved, smoothstep(10.0, 32.0, distToCam));
    float groundBlend = (1.0 - smoothstep(0.0, 0.30, h)) * 0.30;
    float terrainBlend = 1.0 - (1.0 - antiShimmer * 0.80) * (1.0 - groundBlend);
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), terrainBlend));

    // A soft central rib is filtered away when the blade cannot resolve it.
    // No textures, alpha discard, or additional geometry are required.
    float rib = 1.0 - smoothstep(0.0, 0.32, abs(uTransverse));
    baseColor *= 1.0 + (rib - 0.35) * 0.09 * (1.0 - unresolved);
    float wrapped = clamp((dot(n, u_lightDir) + 0.42) / 1.42, 0.0, 1.0);
    float shadow = FoliageShadowVisibility(lightSpace, staticLightSpace, n, u_lightDir);
    float canopy = mix(0.48, 1.0, h * (2.0 - h));

    vec3 baseAmbient = max(u_ambientColor, vec3(0.28, 0.32, 0.24));
    vec3 skyAmbient = baseAmbient * vec3(1.06, 1.10, 1.16);
    vec3 groundBounce = baseAmbient * vec3(0.65, 0.74, 0.44);
    vec3 ambient = mix(groundBounce, skyAmbient, n.y * 0.5 + 0.5);
    vec3 lit = baseColor * ambient * mix(0.68, 1.0, smoothstep(0.0, 0.65, h));

    // Broad, weak highlights with derivative-based normal-variance filtering.
    // Repeated squares replace narrow power lobes on dense foliage.
    vec3 halfDir = normalize(u_lightDir + viewDir);
    vec3 normalDx = dFdx(faceNormal);
    vec3 normalDy = dFdy(faceNormal);
    float variance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
    float nh = max(dot(n, halfDir), 0.0);
    float nh2 = nh * nh;
    float nh4 = nh2 * nh2;
    float spec = nh4 * nh4 * 0.065 * (1.0 - antiShimmer) / (1.0 + 12.0 * variance);
    float forwardScatter = max(dot(-u_lightDir, viewDir), 0.0);
    float backLight = max(-dot(faceNormal, u_lightDir), 0.0);
    float transmission = (backLight * backLight * 0.45
                        + forwardScatter * forwardScatter * 0.30)
                        * smoothstep(0.08, 0.85, h);
    vec3 sunTerms = baseColor * (wrapped * wrapped * 1.05 + spec)
                 + baseColor * vec3(1.25, 1.18, 0.72) * transmission * 0.42;
    lit += sunTerms * u_lightColor * shadow * canopy;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
