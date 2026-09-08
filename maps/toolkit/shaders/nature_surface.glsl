uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;

vec3 NatureShade(vec3 baseColor, vec3 worldPosition, vec3 worldNormal,
                 float heightAlongPlant)
{
    vec3 viewDir = normalize(u_viewPos - worldPosition);
    vec3 geometricNormal = normalize(worldNormal);
    vec3 faceNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;

    // Distinguish upper bloom/petals (height > 0.65) from lower stems and grass blades
    float bloomMask = smoothstep(0.65, 0.90, heightAlongPlant);

    // Stems & grass use strong upward-biased normal (Zelda BotW / Genshin model)
    // for seamless, velvety collective lawn shading without harsh facet borders.
    // Petals maintain their 3D sculpted curvature for rich volume and self-contrast.
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), mix(0.62, 0.12, bloomMask)));

    float frontDiffuse = max(dot(n, u_lightDir), 0.0);
    float wrap = mix(0.24, 0.10, bloomMask);
    float directDiffuse = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);

    // Two-sided Subsurface Scattering (SSS) Transmission:
    // Light penetrates thin foliage membranes when looking towards the sun,
    // producing radiant back-lit glow across grass blades and delicate petals.
    float viewSunAlign = max(dot(-u_lightDir, viewDir), 0.0);
    float transmissionAngle = pow(viewSunAlign, 2.2);
    float thickness = mix(0.15, 1.0, smoothstep(0.05, 0.55, heightAlongPlant));
    float transmission = transmissionAngle * thickness;

    vec3 subsurfaceColor = mix(
        baseColor * vec3(1.35, 1.28, 0.58), // Grass: luminous emerald-gold
        baseColor * vec3(1.42, 1.22, 0.88) + vec3(0.08, 0.04, 0.02), // Petals: radiant warm glow
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
    lit += subsurfaceColor * u_lightColor * transmission * mix(0.52, 0.85, bloomMask);

    // Velvet sheen / micro-specular highlight that shimmers with wind waves
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float sheen = pow(NdotH, 14.0) * mix(0.28, 0.42, bloomMask);
    lit += u_lightColor * sheen * (0.40 + 0.60 * shadow);

    // Soft Fresnel rim lighting along curved petal contours
    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 3.2) * 0.25 * bloomMask;
    lit += baseColor * u_lightColor * rim;

    // Quadratic Root Contact AO (h^2 model): deep ground occlusion at soil level
    // eliminates any floating look and embeds plants securely into the earth.
    float hNorm = clamp(heightAlongPlant, 0.0, 1.0);
    float rootAO = clamp(hNorm * (2.1 - 1.1 * hNorm), 0.14, 1.0);
    lit *= rootAO;

    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
