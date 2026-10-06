#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // Linearized scene depth (world units)
uniform mat4      u_invViewProj;  // Camera inverse ViewProj
uniform vec3      u_camPos;       // Camera position
uniform vec3      u_viewForward;  // Camera forward vector
uniform vec3      u_sunDir;       // Direction light travels (downward)
uniform vec3      u_sunColor;     // Sunlight linear color
uniform vec3      u_fogColor;     // Ambient fog color
uniform float     u_fogDensity;   // Base volume density
uniform float     u_fogStart;     // Near exclusion start distance
uniform float     u_maxDist;      // Max distance clamp
uniform float     u_heightFalloff;// Exponential decay rate k_e along Y
uniform float     u_baseAltitude; // Ground reference altitude Y
uniform float     u_sigmoidEnabled;
uniform vec3      u_sigmoidParams;// x: layerAlt, y: layerThickness, z: layerDensity
uniform float     u_mieAnisotropy;// Henyey-Greenstein g
uniform float     u_godRayIntensity;

// Local Fog Volumes (up to 4 active volumes)
uniform int       u_volumeCount;
uniform vec4      u_volPosShape[4];     // xyz = position, w = shape
uniform vec4      u_volExtentsDense[4]; // xyz = extents, w = density
uniform vec4      u_volColorSoft[4];    // rgb = color, a = edgeSoftness

vec3 ReconstructRayDir(vec2 uv) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_invViewProj * clip;
    return normalize(world.xyz / world.w - u_camPos);
}

float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.001), 1.5));
}

void main() {
    float sceneDepth = texture(texture0, fragTexCoord).r;
    bool isSky = (sceneDepth <= 0.001 || sceneDepth >= 900.0);
    if (isSky) sceneDepth = u_maxDist;

    vec3 rayDir = ReconstructRayDir(fragTexCoord);
    float cosView = max(dot(rayDir, u_viewForward), 0.001);
    float rayDist = min(sceneDepth / cosView, u_maxDist);

    float tStart = max(u_fogStart, 0.5);
    if (rayDist <= tStart + 0.1) {
        finalColor = vec4(0.0);
        return;
    }

    float L = rayDist - tStart;
    vec3 startPos = u_camPos + rayDir * tStart;
    vec3 endPos   = u_camPos + rayDir * rayDist;

    float yStart = startPos.y - u_baseAltitude;
    float yEnd   = endPos.y - u_baseAltitude;
    float dy     = yEnd - yStart;

    // 1. Analytical Exponential Height Fog integral
    // Density(h) = rho0 * exp(-k * h)
    // Integral = rho0 * L * exp(-k * yStart) * (1 - exp(-k * dy)) / (k * dy)
    float k = max(u_heightFalloff, 0.0001);
    float kDy = k * dy;
    float heightFactor;
    if (abs(kDy) > 0.001) {
        heightFactor = (1.0 - exp(-kDy)) / kDy;
    } else {
        heightFactor = 1.0 - 0.5 * kDy;
    }
    float opticalDepth = u_fogDensity * L * exp(-k * max(yStart, -2.0)) * heightFactor;
    opticalDepth = max(opticalDepth, 0.0);

    // 2. Sigmoid thermal inversion / sea of clouds layer
    if (u_sigmoidEnabled > 0.5) {
        float yMid = 0.5 * (yStart + yEnd);
        float diff = (yMid - (u_sigmoidParams.x - u_baseAltitude)) / max(u_sigmoidParams.y, 0.001);
        float sigmDensity = (1.0 / (1.0 + diff * diff)) * u_sigmoidParams.z;
        opticalDepth += sigmDensity * L;
    }

    // 3. Local fog volumes
    vec3 localFogAccum = vec3(0.0);
    float localDensitySum = 0.0;
    for (int i = 0; i < u_volumeCount; i++) {
        vec3 center = u_volPosShape[i].xyz;
        vec3 extents = u_volExtentsDense[i].xyz;
        float baseDen = u_volExtentsDense[i].w;
        vec3 col = u_volColorSoft[i].rgb;
        float softness = clamp(u_volColorSoft[i].a, 0.1, 0.99);

        // Ray to sphere/box centroid distance
        vec3 toCenter = center - startPos;
        float tClosest = clamp(dot(toCenter, rayDir), 0.0, L);
        vec3 pClosest = startPos + rayDir * tClosest;
        vec3 delta = abs(pClosest - center);
        float maxR = max(extents.x, extents.z);
        if (delta.y < extents.y && delta.x < maxR && delta.z < maxR) {
            float dist = length((pClosest - center) / max(extents, vec3(0.001)));
            if (dist < 1.0) {
                float fade = (1.0 - dist) * baseDen * (1.0 / softness);
                localDensitySum += fade;
                localFogAccum += col * fade;
            }
        }
    }
    opticalDepth += localDensitySum;

    // Distant aerial perspective boost for distant terrain and sky horizon
    if (isSky || rayDist > 35.0) {
        float distantBlend = smoothstep(30.0, u_maxDist, rayDist);
        opticalDepth += distantBlend * u_fogDensity * 0.40;
    }

    if (opticalDepth <= 0.0001) {
        finalColor = vec4(0.0);
        return;
    }

    // 4. In-scattered sunlight & Mie forward-scattering
    vec3 sunToLight = normalize(-u_sunDir);
    float cosTheta = clamp(dot(rayDir, sunToLight), -1.0, 1.0);
    float miePhase = HenyeyGreenstein(cosTheta, clamp(u_mieAnisotropy, 0.4, 0.85));
    vec3 directSun = u_sunColor * (miePhase * u_godRayIntensity * 1.6);

    vec3 baseColor = u_fogColor;
    if (localDensitySum > 0.001) {
        baseColor = mix(baseColor, localFogAccum / localDensitySum, clamp(localDensitySum / (opticalDepth + 0.001), 0.0, 1.0));
    }

    // Blend ambient fog with sunlight scattering
    float sunScatteringWeight = clamp(opticalDepth * 0.4, 0.0, 0.85);
    vec3 inScatteredRadiance = mix(baseColor * 0.95, baseColor * 0.5 + directSun, sunScatteringWeight);

    // Beer-Lambert transmittance: T = exp(-tau)
    float transmittance = exp(-opticalDepth);
    float extinction = clamp(1.0 - transmittance, 0.0, 1.0);

    // Premultiplied alpha output
    finalColor = vec4(inScatteredRadiance * extinction, extinction);
}
