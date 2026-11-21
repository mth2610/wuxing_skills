#version 330

in vec3 fragWorldPos;
in float fragLavaDepth;
in float fragShoreFade;

uniform sampler2D texture0;      // cloud_noise.png (tileable FBM noise)
uniform sampler2D u_crackTex;    // lava_voronoi_plates.png (R: plate dome, G: fissure, B: FBM, A: slag)
uniform sampler2D u_lavaFlowTex; // ly_son_lava_flow.png (RG: D8 flow dir, B: shear speed, A: caldera mask)
uniform int u_lavaFlowEnabled;
uniform vec2 u_lavaBounds;

uniform float u_time;
uniform float u_flowSpeed;
uniform vec2 u_flowDirection;
uniform float u_crustCoverage;
uniform float u_glowIntensity;
uniform float u_pulseSpeed;
uniform vec3 u_coreColor;
uniform vec3 u_magmaColor;
uniform vec3 u_crustColor;
uniform vec3 u_viewPos;
uniform vec3 u_ambientColor;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;

out vec4 finalColor;

void main()
{
    // Compute screen-space derivatives in uniform control flow
    vec2 xzDx = dFdx(fragWorldPos.xz);
    vec2 xzDy = dFdy(fragWorldPos.xz);

    // Organic noise-warped shoreline cutout so 1m DEM triangles never show sawtooth edges
    float edgeNoiseA = textureGrad(texture0, fragWorldPos.xz * 0.42 + vec2(1.3, 2.7), xzDx * 0.42, xzDy * 0.42).r;
    float edgeNoiseB = textureGrad(texture0, fragWorldPos.xz * 1.45 - vec2(3.1, 0.8), xzDx * 1.45, xzDy * 1.45).r;
    float shoreWarp = (edgeNoiseA - 0.5) * 0.024 + (edgeNoiseB - 0.5) * 0.010 + 0.004;
    float warpedDepth = max(fragLavaDepth - shoreWarp, 0.0);
    if (warpedDepth < 0.002) discard;

    float t = mod(u_time, 6283.185307);

    // Sample baked D8 flowmap & caldera mask
    vec2 flowDir = (dot(u_flowDirection, u_flowDirection) > 1e-5) ? normalize(u_flowDirection) : vec2(0.0, 1.0);
    float localSpeed = u_flowSpeed;
    float calderaMask = 0.0;
    if (u_lavaFlowEnabled != 0) {
        vec2 flowUV = clamp(fragWorldPos.xz / max(u_lavaBounds, vec2(1.0)), vec2(0.0), vec2(1.0));
        vec4 fSample = textureLod(u_lavaFlowTex, flowUV, 0.0);
        vec2 bakedDir = fSample.rg * 2.0 - 1.0;
        if (dot(bakedDir, bakedDir) > 0.02) {
            flowDir = normalize(bakedDir);
        }
        calderaMask = smoothstep(0.25, 0.75, fSample.a);
        localSpeed = u_flowSpeed * mix(0.45 + 0.85 * fSample.b, 0.14, calderaMask);
    }

    // Domain warp so crust plates drift & shear organically instead of conveyor-belt scrolling
    vec2 warpSample = textureGrad(texture0, fragWorldPos.xz * 0.065 + vec2(0.0, -t * 0.015), xzDx * 0.065, xzDy * 0.065).rg;
    vec2 domainWarp = (warpSample - 0.5) * mix(1.35, 2.40, calderaMask);

    // Dual-phase flow advection along local D8 stream vector (or slow cellular convection in caldera)
    float flowPhase = fract(t * 0.12);
    float flowPhase2 = fract(flowPhase + 0.5);
    float flowBlend = abs(2.0 * flowPhase - 1.0);

    vec2 advect = flowDir * localSpeed * 0.75;
    float plateScale = mix(0.11, 0.075, calderaMask);
    vec2 baseUV = (fragWorldPos.xz + domainWarp) * plateScale;
    vec2 uv0 = baseUV - advect * flowPhase;
    vec2 uv1 = baseUV - advect * flowPhase2 + vec2(0.37, 0.61);

    vec4 p0 = textureGrad(u_crackTex, uv0, xzDx * plateScale, xzDy * plateScale);
    vec4 p1 = textureGrad(u_crackTex, uv1, xzDx * plateScale, xzDy * plateScale);
    vec4 plates = mix(p0, p1, flowBlend);

    // Secondary fine cooling cracks rotated 37 deg to break repetition
    vec2 rotUV = vec2(fragWorldPos.x * 0.798 - fragWorldPos.z * 0.602,
                      fragWorldPos.x * 0.602 + fragWorldPos.z * 0.798) * 0.18 + domainWarp * 0.08;
    vec4 subPlates = textureGrad(u_crackTex, rotUV - advect * 0.35 * flowPhase, xzDx * 0.18, xzDy * 0.18);

    float plateDome = mix(plates.r, subPlates.r, 0.25);
    float fissureNet = clamp(max(plates.g, subPlates.g * 0.55), 0.0, 1.0);
    float fbmTurb = plates.b * 0.70 + subPlates.b * 0.30;
    float slagRough = clamp(0.38 + plates.a * 0.52, 0.32, 0.94);

    // Analytical 3D normal reconstruction from Voronoi plate dome height for crisp basalt relief
    vec2 dPlateOff = vec2(0.012, 0.0);
    float hR = mix(textureGrad(u_crackTex, uv0 + dPlateOff.xy, xzDx * plateScale, xzDy * plateScale).r,
                   textureGrad(u_crackTex, uv1 + dPlateOff.xy, xzDx * plateScale, xzDy * plateScale).r, flowBlend);
    float hU = mix(textureGrad(u_crackTex, uv0 + dPlateOff.yx, xzDx * plateScale, xzDy * plateScale).r,
                   textureGrad(u_crackTex, uv1 + dPlateOff.yx, xzDx * plateScale, xzDy * plateScale).r, flowBlend);
    vec3 crustNormal = normalize(vec3((plateDome - hR) * 3.8, 1.0, (plateDome - hU) * 3.8));

    // Depth & convective thermal pulse: deep caldera center & river thalweg glow hotter; banks crust over
    float depthHeat = smoothstep(0.04, 0.58, fragLavaDepth);
    float pulse = 0.88 + 0.12 * sin(t * u_pulseSpeed + fbmTurb * 6.2831853);

    // Selective crust coverage: 75%+ dark cooling basalt rafts, 15-25% incandescent magma tears
    float crustThresh = clamp(u_crustCoverage + (1.0 - depthHeat) * 0.28 - calderaMask * 0.06, 0.18, 0.90);
    float plateSignal = plateDome * 0.74 + fbmTurb * 0.26;
    float crustMask = smoothstep(crustThresh - 0.16, crustThresh + 0.08, plateSignal);

    float fissureHeat = smoothstep(0.28, 0.86, fissureNet) * (0.28 + 0.72 * depthHeat);
    float openMagma = (1.0 - crustMask) * (0.45 + 0.55 * depthHeat);
    float moltenWeight = clamp(max(openMagma, fissureHeat * 0.92), 0.0, 1.0);

    // Blackbody temperature ramp: dark red slag -> crimson -> fiery orange -> incandescent white-gold core
    float heat = clamp(moltenWeight * (0.42 + 0.58 * depthHeat) * pulse, 0.0, 1.0);
    vec3 darkSlagRed = u_magmaColor * vec3(0.45, 0.08, 0.02);
    vec3 magmaRgb = mix(darkSlagRed, u_magmaColor, smoothstep(0.18, 0.60, heat));
    magmaRgb = mix(magmaRgb, u_coreColor, smoothstep(0.68, 0.96, heat));

    // Selective HDR emission only in the hottest tears & cores so bloom wraps crisply
    float hdrGain = u_glowIntensity * (0.35 + 1.15 * heat * heat);
    vec3 emissiveMagma = magmaRgb * hdrGain;

    // 3D Lit Cooled Basalt Crust: directional sun diffuse + cool sky hemisphere + glossy slag specular
    vec3 viewDir = normalize(u_viewPos - fragWorldPos);
    vec3 lDir = (dot(u_lightDir, u_lightDir) > 0.001) ? normalize(u_lightDir) : normalize(vec3(0.6, 0.5, -0.5));
    float NdotL = max(dot(crustNormal, lDir), 0.0);
    vec3 halfVec = normalize(lDir + viewDir);
    float shininess = mix(42.0, 8.0, slagRough);
    float spec = pow(max(dot(crustNormal, halfVec), 0.0), shininess) * mix(0.28, 0.04, slagRough);
    float fresnel = pow(1.0 - max(dot(crustNormal, viewDir), 0.0), 3.2);

    vec3 basaltAlbedo = u_crustColor * (0.75 + 0.50 * plateDome);
    vec3 crustLit = basaltAlbedo * (u_ambientColor * (0.65 + 0.45 * crustNormal.y) + u_lightColor * NdotL * 0.55)
                  + u_lightColor * spec * 0.45
                  + u_ambientColor * (0.14 * fresnel)
                  + u_magmaColor * (0.12 * (1.0 - plateDome) * depthHeat);

    vec3 color = mix(crustLit, emissiveMagma, moltenWeight);
    // Dark solidified slag bank meniscus welding seamlessly into the ground (no bright neon edges)
    float bankCool = smoothstep(0.002, 0.14, warpedDepth);
    color = mix(crustLit * 0.80, color, bankCool);

    finalColor = vec4(color, 1.0);
}
