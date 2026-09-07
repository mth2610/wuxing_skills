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

    // Stems/grass use an upward-biased normal for soft collective lawn lighting.
    // Flower petals keep their true 3D geometric curvature for rich shape, contrast, and depth.
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), 0.34 * (1.0 - bloomMask)));

    float frontDiffuse = max(dot(n, u_lightDir), 0.0);
    float wrap = mix(0.18, 0.08, bloomMask);
    float wrappedDiffuse = clamp((dot(n, u_lightDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float directDiffuse = mix(frontDiffuse, wrappedDiffuse, mix(0.42, 0.15, bloomMask));

    // Translucency / Subsurface Scattering: thin petals glow warmly when backlit
    float backLight = max(dot(-faceNormal, u_lightDir), 0.0);
    float viewScatter = 0.50 + 0.50 * pow(max(dot(-u_lightDir, viewDir), 0.0), 2.2);
    float transmission = pow(backLight, 1.35) * viewScatter;
    vec3 subsurfaceColor = mix(baseColor, baseColor * vec3(1.30, 1.20, 0.72) + vec3(0.06, 0.04, 0.01), 0.65);

    float horizon = 0.70 + 0.30 * max(n.y, 0.0);
    float staticSlope = 1.0 - max(dot(n, normalize(u_lightDir)), 0.0);
    float shadow = MapStaticShadowVisibility(worldPosition, staticSlope);

    // Flower cup cavity ambient occlusion: darker inside the flower center disc
    float cupCavity = mix(0.55, 1.0, smoothstep(0.70, 0.98, heightAlongPlant));
    vec3 ambientFloor = max(u_ambientColor, vec3(0.24, 0.265, 0.205)) * mix(1.0, cupCavity, bloomMask);

    vec3 direct = u_lightColor * directDiffuse * shadow;
    float ambientVisibility = mix(0.94, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;
    lit += baseColor * direct * 0.94;

    // Subsurface transmission glow
    lit += subsurfaceColor * u_lightColor * transmission * (0.30 + 0.65 * bloomMask);

    // Velvet sheen / micro-specular highlight on petals
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float NdotH = max(dot(faceNormal, halfDir), 0.0);
    float sheen = pow(NdotH, 16.0) * 0.30 * bloomMask;
    lit += u_lightColor * sheen * (0.45 + 0.55 * shadow);

    // Soft Fresnel rim lighting along curved petal contours
    float NdotV = max(dot(faceNormal, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 3.2) * 0.25 * bloomMask;
    lit += baseColor * u_lightColor * rim;

    float rootOcclusion = mix(0.86, 1.0, smoothstep(0.0, 0.58, heightAlongPlant));
    lit *= rootOcclusion;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
