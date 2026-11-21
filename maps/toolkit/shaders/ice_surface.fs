#version 330

in vec3 fragWorldPos;
in float fragIceDepth;
in float fragShoreFade;

uniform sampler2D texture0;      // cloud_noise.png
uniform sampler2D u_plateTex;    // glacial_ice_plates.png (R: floe dome, G: fracture net, B: bubbles, A: snow crust)
uniform sampler2D u_iceFlowTex;  // mau_son_ice_flow.png (RG: D8 flow dir, B: freeze weight, A: lake mask)
uniform int u_iceFlowEnabled;
uniform vec2 u_iceBounds;

uniform float u_time;
uniform float u_driftSpeed;
uniform vec2 u_driftDirection;
uniform float u_floeCoverage;
uniform float u_crackIntensity;
uniform float u_snowCrustAmount;
uniform vec3 u_deepIceColor;
uniform vec3 u_shallowIceColor;
uniform vec3 u_snowRimeColor;
uniform vec3 u_viewPos;
uniform vec3 u_ambientColor;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;

out vec4 finalColor;

void main()
{
    vec2 xzDx = dFdx(fragWorldPos.xz);
    vec2 xzDy = dFdy(fragWorldPos.xz);

    // Organic shoreline cutout matching water_surface.fs
    float edgeNoiseA = textureGrad(texture0, fragWorldPos.xz * 0.45 + vec2(1.7, 2.3), xzDx * 0.45, xzDy * 0.45).r;
    float edgeNoiseB = textureGrad(texture0, fragWorldPos.xz * 1.35 - vec2(2.8, 1.1), xzDx * 1.35, xzDy * 1.35).r;
    float shoreWarp = (edgeNoiseA - 0.5) * 0.022 + (edgeNoiseB - 0.5) * 0.010 + 0.004;
    float warpedDepth = max(fragIceDepth - shoreWarp, 0.0);
    if (warpedDepth < 0.003) discard;

    float t = mod(u_time, 6283.185307);

    // Sample baked D8 flowmap & freeze potential
    vec2 flowDir = (dot(u_driftDirection, u_driftDirection) > 1e-5) ? normalize(u_driftDirection) : vec2(0.65, 0.55);
    float freezeWeight = 0.60;
    float lakeMask = 0.50;
    if (u_iceFlowEnabled != 0) {
        vec2 flowUV = clamp(fragWorldPos.xz / max(u_iceBounds, vec2(1.0)), vec2(0.0), vec2(1.0));
        vec4 fSample = textureLod(u_iceFlowTex, flowUV, 0.0);
        vec2 bakedDir = fSample.rg * 2.0 - 1.0;
        if (dot(bakedDir, bakedDir) > 0.02) {
            flowDir = normalize(bakedDir);
        }
        freezeWeight = fSample.b;
        lakeMask = smoothstep(0.20, 0.75, fSample.a);
    }

    // Shore-fast ice shelf in shallow water (< 0.22m) + broken floating Voronoi ice floes mid-lake
    float shoreShelf = 1.0 - smoothstep(0.08, 0.26, warpedDepth);
    float slowDrift = u_driftSpeed * (1.0 - shoreShelf * 0.92);
    vec2 driftOffset = flowDir * (sin(t * slowDrift * 0.5) * 0.35 + t * slowDrift * 0.12);

    float plateScale = mix(0.11, 0.082, lakeMask);
    vec2 baseUV = (fragWorldPos.xz - driftOffset) * plateScale;
    vec4 plates = textureGrad(u_plateTex, baseUV, xzDx * plateScale, xzDy * plateScale);

    // Secondary rotated fracture network for authentic glacial cleavage
    vec2 rotUV = vec2(fragWorldPos.x * 0.809 - fragWorldPos.z * 0.588,
                      fragWorldPos.x * 0.588 + fragWorldPos.z * 0.809) * 0.15;
    vec4 subPlates = textureGrad(u_plateTex, rotUV, xzDx * 0.15, xzDy * 0.15);

    float floeSignal = plates.r * 0.72 + subPlates.r * 0.28;
    float shelfBias = shoreShelf * 0.24 + freezeWeight * 0.10;
    float floeThreshold = clamp(1.0 - u_floeCoverage - shelfBias, 0.18, 0.90);
    float floeAlpha = smoothstep(floeThreshold - 0.04, floeThreshold + 0.06, floeSignal + (edgeNoiseA - 0.5) * 0.14);
    if (floeAlpha < 0.14) discard;

    // Reconstruct crisp bevelled floe normal from plate dome height
    vec2 dOff = vec2(0.012, 0.0);
    float hR = textureGrad(u_plateTex, baseUV + dOff.xy, xzDx * plateScale, xzDy * plateScale).r;
    float hU = textureGrad(u_plateTex, baseUV + dOff.yx, xzDx * plateScale, xzDy * plateScale).r;
    vec3 iceNormal = normalize(vec3((plates.r - hR) * 2.4, 1.0, (plates.r - hU) * 2.4));

    vec3 viewVec = u_viewPos - fragWorldPos;
    vec3 viewDir = viewVec * inversesqrt(max(dot(viewVec, viewVec), 1e-8));
    vec3 lDir = (dot(u_lightDir, u_lightDir) > 0.001) ? normalize(u_lightDir) : normalize(vec3(0.6, 0.5, -0.5));

    // 2-Layer Parallax Internal Fracture & Trapped Air Bubble Refraction (n_ice = 1.31)
    vec3 R_ice = refract(-viewDir, iceNormal, 0.7633588);
    if (dot(R_ice, R_ice) < 0.01) R_ice = -viewDir;
    vec2 parallaxUV = baseUV + R_ice.xz * 0.085;
    vec4 deepPlates = textureGrad(u_plateTex, parallaxUV, xzDx * plateScale, xzDy * plateScale);

    float surfaceCrack = plates.g;
    float deepCrack = deepPlates.g;
    float fractureGlow = clamp(surfaceCrack * 0.65 + deepCrack * 0.85, 0.0, 1.0) * u_crackIntensity;
    float bubbles = deepPlates.b;

    // Ice thickness & optical depth color
    float iceThickness = clamp(floeSignal * 0.7 + shoreShelf * 0.3, 0.0, 1.0);
    vec3 iceBody = mix(u_shallowIceColor * 0.82, u_deepIceColor, smoothstep(0.15, 0.85, fragIceDepth * 0.65 + bubbles * 0.35));
    iceBody += vec3(0.28, 0.56, 0.72) * (fractureGlow * 0.38);
    iceBody = mix(iceBody, u_shallowIceColor * 1.12, bubbles * 0.24);

    // Surface wind-blown snow crust & hoarfrost rime on thick floe interiors
    float snowCrustMask = clamp(plates.a * u_snowCrustAmount * smoothstep(0.25, 0.75, floeAlpha), 0.0, 0.92);
    vec3 albedo = mix(iceBody, u_snowRimeColor, snowCrustMask);

    // Lighting: crisp Fresnel sky reflection + icy GGX specular + glacial SSS
    float NdotL = max((dot(iceNormal, lDir) + 0.25) * 0.8, 0.0);
    float NdotV = max(dot(iceNormal, viewDir), 0.001);
    float omv = 1.0 - NdotV;
    float omv2 = omv * omv;
    float fresnel = 0.04 + 0.96 * (omv2 * omv2 * omv);

    vec3 halfVec = normalize(lDir + viewDir);
    float NdotH = max(dot(iceNormal, halfVec), 0.0);
    float specPower = mix(96.0, 14.0, snowCrustMask);
    float spec = pow(NdotH, specPower) * mix(0.68, 0.08, snowCrustMask);

    float backLight = pow(max(dot(viewDir, -lDir), 0.0), 3.0);
    vec3 iceSSS = u_shallowIceColor * (backLight * 0.35 * (1.0 - snowCrustMask * 0.7));

    vec3 skyReflect = mix(u_ambientColor * 1.25 + vec3(0.12, 0.18, 0.26), u_lightColor * 0.55, 0.25);
    vec3 lit = albedo * (u_ambientColor * 0.92 + u_lightColor * NdotL * 0.85)
             + iceSSS * u_lightColor
             + u_lightColor * spec;
    lit = mix(lit, skyReflect, fresnel * (1.0 - snowCrustMask * 0.82) * 0.52);

    // Crisp frosted ice-edge bevel highlight
    float edgeBevel = smoothstep(0.12, 0.45, floeAlpha) * (1.0 - smoothstep(0.45, 0.95, floeAlpha));
    lit += u_snowRimeColor * (edgeBevel * 0.24 * (u_ambientColor + u_lightColor * 0.4));

    finalColor = vec4(lit, 1.0);
}
