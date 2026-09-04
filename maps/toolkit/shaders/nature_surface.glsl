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
    vec3 n = normalize(mix(faceNormal, vec3(0.0, 1.0, 0.0), 0.34));
    float frontDiffuse = max(dot(n, u_lightDir), 0.0);
    float wrappedDiffuse = clamp((dot(n, u_lightDir) + 0.18) / 1.18, 0.0, 1.0);
    float backLight = max(dot(-faceNormal, u_lightDir), 0.0);
    float viewScatter = 0.58 + 0.42 * pow(max(dot(-u_lightDir, viewDir), 0.0), 2.0);
    float transmission = pow(backLight, 1.45) * viewScatter;
    float horizon = 0.70 + 0.30 * max(n.y, 0.0);
    float shadow = MapShadowVisibility(worldPosition, n, u_lightDir);
    vec3 ambientFloor = max(u_ambientColor, vec3(0.20, 0.225, 0.17));
    vec3 direct = u_lightColor * mix(frontDiffuse, wrappedDiffuse, 0.42) * shadow;
    vec3 subsurfaceColor = mix(baseColor, baseColor * vec3(1.12, 1.20, 0.72), 0.48);
    float ambientVisibility = mix(0.72, 1.0, shadow);
    vec3 lit = baseColor * ambientFloor * horizon * ambientVisibility;
    lit += baseColor * direct * 0.94;
    lit += subsurfaceColor * u_lightColor * transmission * shadow * 0.30;
    float rootOcclusion = mix(0.74, 1.0, smoothstep(0.0, 0.58, heightAlongPlant));
    lit *= rootOcclusion;
    lit += VFXLights_Accumulate(worldPosition, n, baseColor);
    return lit;
}
