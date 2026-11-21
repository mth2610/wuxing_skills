#version 330

in vec3 fragWorldPos;
in float fragLavaDepth;
in float fragShoreFade;

uniform sampler2D texture0;      // cloud_noise.png (tileable FBM noise)
uniform sampler2D u_crackTex;    // tex_crack_mask.png (thermal basalt fissures)
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
    vec2 flowDir = (dot(u_flowDirection, u_flowDirection) > 1e-5) ? normalize(u_flowDirection) : vec2(0.0, 1.0);
    vec2 flowVec = flowDir * u_flowSpeed;

    // Dual-phase flow advection to prevent UV stretching on moving magma channels
    float flowPhase = fract(t * 0.14);
    float flowPhase2 = fract(flowPhase + 0.5);
    float flowBlend = abs(2.0 * flowPhase - 1.0);

    vec2 baseUV = fragWorldPos.xz * 0.14;
    vec2 uv0 = baseUV - flowVec * flowPhase * 0.55;
    vec2 uv1 = baseUV - flowVec * flowPhase2 * 0.55 + vec2(3.7, 5.1);

    float n0 = textureGrad(texture0, uv0, xzDx * 0.14, xzDy * 0.14).r;
    float n1 = textureGrad(texture0, uv1, xzDx * 0.14, xzDy * 0.14).r;
    float magmaNoise = mix(n0, n1, flowBlend);

    // Crack mask for incandescent thermal fissures separating cooled basalt crust plates
    vec2 crackUV0 = fragWorldPos.xz * 0.24 - flowVec * flowPhase * 0.35;
    vec2 crackUV1 = fragWorldPos.xz * 0.24 - flowVec * flowPhase2 * 0.35 + vec2(1.9, 4.3);
    float c0 = textureGrad(u_crackTex, crackUV0, xzDx * 0.24, xzDy * 0.24).r;
    float c1 = textureGrad(u_crackTex, crackUV1, xzDx * 0.24, xzDy * 0.24).r;
    float crackMask = mix(c0, c1, flowBlend);

    // Depth & convective thermal pulse: deep caldera center stays hot & molten, shallow banks crust over
    float depthHeat = smoothstep(0.04, 0.55, fragLavaDepth);
    float pulse = 0.88 + 0.12 * sin(t * u_pulseSpeed + magmaNoise * 6.2831853);

    float crustThresh = clamp(u_crustCoverage + (1.0 - depthHeat) * 0.26, 0.15, 0.92);
    float plateSignal = magmaNoise * 0.65 + (1.0 - crackMask) * 0.35;
    float crustMask = smoothstep(crustThresh - 0.14, crustThresh + 0.10, plateSignal);

    // Narrow glowing fissure veins even inside cooled crust plates
    float fissureGlow = smoothstep(0.45, 0.88, crackMask) * (0.35 + 0.65 * depthHeat);
    float moltenWeight = clamp(max(1.0 - crustMask, fissureGlow * 0.85), 0.0, 1.0);

    // Blackbody temperature ramp: dark slag -> crimson -> bright orange magma -> incandescent yellow-white core
    float heat = clamp(moltenWeight * (0.45 + 0.55 * depthHeat) * pulse, 0.0, 1.0);
    vec3 crimson = u_magmaColor * vec3(0.75, 0.22, 0.06);
    vec3 magmaRgb = mix(crimson, u_magmaColor, smoothstep(0.15, 0.62, heat));
    magmaRgb = mix(magmaRgb, u_coreColor, smoothstep(0.62, 0.96, heat));

    // HDR blackbody emission above bloom threshold
    vec3 emissiveMagma = magmaRgb * (u_glowIntensity * (0.55 + 0.95 * heat));

    // Cooled vesicular basalt slag crust with subtle Fresnel rim & ambient fill
    vec3 viewDir = normalize(u_viewPos - fragWorldPos);
    float fresnel = pow(1.0 - max(viewDir.y, 0.0), 3.0);
    vec3 crustLit = u_crustColor * (0.45 + u_ambientColor * 0.85) + u_magmaColor * (0.08 * fresnel);

    vec3 color = mix(crustLit, emissiveMagma, moltenWeight);
    // Thin cooling rim right at the solid terrain bank
    float bankCool = smoothstep(0.002, 0.09, warpedDepth);
    color = mix(crustLit * 0.85, color, bankCool);

    finalColor = vec4(color, 1.0);
}
