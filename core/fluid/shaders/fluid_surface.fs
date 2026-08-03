#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // smoothed nearest fluid depth
uniform sampler2D u_thicknessTex; // additive optical path proxy
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
uniform vec2 u_texel;      // fluid reconstruction texel
uniform vec2 u_sceneTexel; // full-resolution scene texel
uniform int u_hasSceneDepth;
uniform int u_qualityTier;

uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_sunDirectionView;  // surface -> sun, view space
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform vec3 u_materialBody;
uniform vec3 u_materialGlow;
uniform vec3 u_materialSoft;
uniform float u_time;

#define FLUID_POINT_LIGHTS 4
uniform int u_pointLightCount;
uniform vec4 u_pointLightPosRadius[FLUID_POINT_LIGHTS];
uniform vec4 u_pointLightColor[FLUID_POINT_LIGHTS];

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

float FresnelSchlick(float ndv) {
    const float waterF0 = 0.02037; // ((1.333 - 1)/(1.333 + 1))^2
    return waterF0 + (1.0 - waterF0) * pow(1.0 - ndv, 5.0);
}

// Multi-octave procedural wave generator for realistic dynamic liquid shimmer
vec3 WaterMultiOctaveWaves(vec3 worldPosition) {
    float t = u_time * 1.2;
    vec2 p1 = worldPosition.xz * 8.5 + vec2(t * 0.8, t * 0.6);
    vec2 p2 = worldPosition.xz * 18.0 - vec2(t * 0.6, -t * 0.9);
    vec2 p3 = worldPosition.xz * 36.0 + vec2(-t * 1.1, t * 1.4);

    float w1 = sin(p1.x + sin(p1.y * 1.2));
    float w2 = sin(p2.y + sin(p2.x * 1.1));
    float w3 = sin(p3.x * 0.7 + p3.y * 0.7);

    vec2 dh = vec2(
        cos(p1.x) * 0.025 + cos(p2.x) * 0.015 + cos(p3.x) * 0.008,
        cos(p1.y) * 0.025 + cos(p2.y) * 0.015 + cos(p3.y) * 0.008
    );
    return vec3(-dh.x, 1.0, -dh.y);
}

float DecodeOpticalThickness(float encodedThickness) {
    float accumulatedPath = max(encodedThickness / 16.0, 0.0);
    return 0.16 * (1.0 - exp(-accumulatedPath / 0.11));
}

float WaterSpecularBRDF(vec3 N, vec3 V, vec3 L, float roughness) {
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return 0.0;
    vec3 H = normalize(V + L);
    float ndh = max(dot(N, H), 0.0);
    float vdh = max(dot(V, H), 0.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / max(3.14159265 * denominator * denominator, 1e-5);
    float k = (roughness + 1.0);
    k = k * k * 0.125;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    float F = 0.02037 + (1.0 - 0.02037) * pow(1.0 - vdh, 5.0);
    return min(D * gv * gl * F / max(4.0 * ndl * ndv, 1e-4), 8.0);
}

bool SceneSampleMatchesBase(vec2 uv, float baseSceneDistance, float fluidDistance) {
    if (u_hasSceneDepth == 0) return true;
    float sceneDepth = texture(u_sceneDepthTex, uv).r;
    bool baseHasGeometry = baseSceneDistance < 1e19;
    bool sampleHasGeometry = sceneDepth < 0.99999;
    if (baseHasGeometry != sampleHasGeometry) return false;
    if (!sampleHasGeometry) return true;
    vec3 scenePosition = ReconstructViewPosition(uv, sceneDepth);
    float sampleDistance = -scenePosition.z;
    if (sampleDistance <= fluidDistance + 0.002) return false;
    float layerTolerance = max(0.060, baseSceneDistance * 0.018);
    return abs(sampleDistance - baseSceneDistance) <= layerTolerance;
}

// Lightweight Screen Space Reflection (SSR) raymarcher
vec4 TraceSSR(vec3 rayOrigin, vec3 rayDir, float fluidDistance) {
    if (u_hasSceneDepth == 0 || rayDir.z >= 0.05) return vec4(0.0);
    
    float stepSize = 0.04;
    vec3 currentRay = rayOrigin + rayDir * stepSize;
    
    for (int i = 0; i < 10; i++) {
        vec4 clip = u_projection * vec4(currentRay, 1.0);
        if (clip.w <= 0.0) break;
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sceneDepth = texture(u_sceneDepthTex, sampleUV).r;
        if (sceneDepth < 0.99999) {
            vec3 scenePos = ReconstructViewPosition(sampleUV, sceneDepth);
            float sceneDist = -scenePos.z;
            float rayDist = -currentRay.z;
            
            if (rayDist >= sceneDist && rayDist - sceneDist < 0.25) {
                // Binary refinement step
                currentRay -= rayDir * (stepSize * 0.5);
                clip = u_projection * vec4(currentRay, 1.0);
                sampleUV = clamp(clip.xy / clip.w * 0.5 + 0.5, 0.001, 0.999);
                
                vec3 hitColor = texture(u_sceneTex, sampleUV).rgb;
                float edgeFade = smoothstep(0.0, 0.08, sampleUV.x) * smoothstep(1.0, 0.92, sampleUV.x) *
                                 smoothstep(0.0, 0.08, sampleUV.y) * smoothstep(1.0, 0.92, sampleUV.y);
                return vec4(hitColor, edgeFade * 0.85);
            }
        }
        currentRay += rayDir * stepSize;
        stepSize *= 1.25;
    }
    return vec4(0.0);
}

void main() {
    float fluidDepth = texture(texture0, fragTexCoord).r;
    if (fluidDepth >= 0.99999) discard;

    vec3 positionView = ReconstructViewPosition(fragTexCoord, fluidDepth);
    vec3 V = normalize(-positionView);
    float fluidDistance = -positionView.z;

    float sceneDepthAtSurface = 1.0;
    float sceneDistanceAtSurface = 1e20;
    float intersectionVisibility = 1.0;
    if (u_hasSceneDepth != 0) {
        sceneDepthAtSurface = texture(u_sceneDepthTex, fragTexCoord).r;
        if (sceneDepthAtSurface < 0.99999) {
            vec3 sceneAtSurface = ReconstructViewPosition(fragTexCoord, sceneDepthAtSurface);
            sceneDistanceAtSurface = -sceneAtSurface.z;
            float frontGap = sceneDistanceAtSurface - fluidDistance;
            if (frontGap <= -0.002) discard;
            intersectionVisibility = smoothstep(-0.002, 0.012, frontGap);
        }
    }

    // Minimum Gradient Normal Reconstruction: compare forward/backward pairs
    // to prevent edge bleeding and sharp 90-degree normal spikes at splat edges.
    float depthLeft = texture(texture0, fragTexCoord - vec2(u_texel.x, 0.0)).r;
    float depthRight = texture(texture0, fragTexCoord + vec2(u_texel.x, 0.0)).r;
    float depthDown = texture(texture0, fragTexCoord - vec2(0.0, u_texel.y)).r;
    float depthUp = texture(texture0, fragTexCoord + vec2(0.0, u_texel.y)).r;
    
    depthLeft = depthLeft >= 0.99999 ? fluidDepth : depthLeft;
    depthRight = depthRight >= 0.99999 ? fluidDepth : depthRight;
    depthDown = depthDown >= 0.99999 ? fluidDepth : depthDown;
    depthUp = depthUp >= 0.99999 ? fluidDepth : depthUp;

    vec3 posL = ReconstructViewPosition(fragTexCoord - vec2(u_texel.x, 0.0), depthLeft);
    vec3 posR = ReconstructViewPosition(fragTexCoord + vec2(u_texel.x, 0.0), depthRight);
    vec3 posD = ReconstructViewPosition(fragTexCoord - vec2(0.0, u_texel.y), depthDown);
    vec3 posU = ReconstructViewPosition(fragTexCoord + vec2(0.0, u_texel.y), depthUp);

    vec3 dx1 = positionView - posL;
    vec3 dx2 = posR - positionView;
    vec3 dy1 = positionView - posD;
    vec3 dy2 = posU - positionView;

    vec3 dx = abs(dx1.z) < abs(dx2.z) ? dx1 : dx2;
    vec3 dy = abs(dy1.z) < abs(dy2.z) ? dy1 : dy2;

    vec3 N = normalize(cross(dx, dy));
    if (dot(N, V) < 0.0) N = -N;

    // Apply multi-octave dynamic wave perturbation
    vec3 worldPosition = (u_viewToWorld * vec4(positionView, 1.0)).xyz;
    vec3 worldNormal = normalize(mat3(u_viewToWorld) * N);
    vec3 waveNorm = WaterMultiOctaveWaves(worldPosition);
    worldNormal = normalize(worldNormal + waveNorm * 0.029);
    N = normalize(transpose(mat3(u_viewToWorld)) * worldNormal);
    if (dot(N, V) < 0.0) N = -N;
    float ndv = clamp(dot(N, V), 0.0, 1.0);

    float thicknessProxy = max(texture(u_thicknessTex, fragTexCoord).r, 0.0);
    float kernelThickness = DecodeOpticalThickness(thicknessProxy);
    float surfaceCoverage = smoothstep(0.0004, 0.010, kernelThickness) * intersectionVisibility;
    
    float sceneGap = 1.0;
    float waterColumnDepth = kernelThickness;
    if (sceneDepthAtSurface < 0.99999) {
        float depthGap = max(sceneDistanceAtSurface - fluidDistance, 0.0);
        sceneGap = depthGap;
        waterColumnDepth = max(kernelThickness, min(0.40, depthGap * 0.90));
    }
    
    float opticalPath = min(waterColumnDepth / max(ndv, 0.22), 0.50);
    float airborneWeight = smoothstep(0.070, 0.220, sceneGap);

    // Snell Refraction
    vec3 incident = -V;
    vec3 refractedDirection = refract(incident, N, 1.0 / 1.333);
    vec2 incidentSlope = incident.xy / max(abs(incident.z), 0.25);
    vec2 refractedSlope = refractedDirection.xy / max(abs(refractedDirection.z), 0.25);
    float travelPixels = clamp(opticalPath * 92.0, 0.35, 10.0);
    vec2 refractOffset = (refractedSlope - incidentSlope) * travelPixels * u_sceneTexel;
    vec2 refractUV = clamp(fragTexCoord + refractOffset, u_sceneTexel, vec2(1.0) - u_sceneTexel);

    if (!SceneSampleMatchesBase(refractUV, sceneDistanceAtSurface, fluidDistance)) {
        refractUV = mix(refractUV, fragTexCoord, 0.70);
    }

    vec3 refractedScene;
    if (u_qualityTier >= 2) {
        vec2 dispersion = refractOffset * mix(0.018, 0.045, 1.0 - ndv);
        vec2 refractUVRed = clamp(refractUV + dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        vec2 refractUVBlue = clamp(refractUV - dispersion, u_sceneTexel, vec2(1.0) - u_sceneTexel);
        refractedScene.r = texture(u_sceneTex, refractUVRed).r;
        refractedScene.g = texture(u_sceneTex, refractUV).g;
        refractedScene.b = texture(u_sceneTex, refractUVBlue).b;
    } else {
        refractedScene = texture(u_sceneTex, refractUV).rgb;
    }

    // Underwater Caustics Projection
    if (sceneDepthAtSurface < 0.99999 && opticalPath > 0.005) {
        vec3 underPosView = ReconstructViewPosition(fragTexCoord, sceneDepthAtSurface);
        vec3 underPosWorld = (u_viewToWorld * vec4(underPosView, 1.0)).xyz;
        vec2 cUV = underPosWorld.xz * 5.0 + u_sunDirectionView.xz * 1.5 + vec2(u_time * 1.1, u_time * 0.85);
        float causticNoise = sin(cUV.x + sin(cUV.y * 1.4)) * sin(cUV.y + sin(cUV.x * 1.3));
        float causticPattern = pow(max(0.0, 0.5 + 0.5 * causticNoise), 3.0) * 1.6;
        float causticFade = smoothstep(0.0, 0.03, sceneGap) * smoothstep(0.40, 0.05, sceneGap);
        vec3 causticColor = u_sunColor * causticPattern * causticFade * 0.40;
        refractedScene += causticColor;
    }

    // Beer-Lambert Volumetric Absorption
    float materialPeak = max(max(u_materialBody.r, u_materialBody.g), max(u_materialBody.b, 0.001));
    vec3 materialTransmission = clamp(u_materialBody / materialPeak, vec3(0.035), vec3(1.0));
    vec3 absorption = -log(materialTransmission) * 1.75 + vec3(0.06);
    vec3 transmittance = exp(-absorption * opticalPath);

    float hemi = clamp(worldNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(u_groundAmbient, u_skyAmbient, hemi);
    vec3 L = normalize(u_sunDirectionView);
    float ndl = max(dot(N, L), 0.0);

    vec3 waterScatterColor = u_materialBody;
    vec3 illumination = ambient + u_sunColor * (0.12 + 0.30 * ndl);
    vec3 transmitted = refractedScene * transmittance;
    float illuminationEnergy = dot(illumination, vec3(0.2126, 0.7152, 0.0722));
    float backgroundScatter = 1.0 - dot(transmittance, vec3(0.2126, 0.7152, 0.0722));
    float scatterAmount = clamp(0.30 + 0.70 * backgroundScatter, 0.0, 1.0);
    vec3 inScatter = waterScatterColor * scatterAmount * (0.34 + illuminationEnergy * 0.42);

    float fresnel = FresnelSchlick(ndv);
    float volumeWeight = smoothstep(0.016, 0.095, kernelThickness);
    float visibleFresnel = fresnel * mix(0.045, 1.0, volumeWeight);
    // Fresnel rim: keeps the silhouette legible on any background (bright
    // grass included) without painting a cartoon outline - it fades to zero
    // on thin edges and is lit by the sun.
    float rimStrength = pow(1.0 - ndv, 2.0) * smoothstep(0.012, 0.10, kernelThickness);
    vec3 rimLight = u_materialGlow * rimStrength * (0.12 + 0.55 * illuminationEnergy) * (0.30 + 0.70 * ndl);
    vec3 viewWorld = normalize(mat3(u_viewToWorld) * V);
    vec3 reflectedWorld = reflect(-viewWorld, worldNormal);
    float skyReflection = smoothstep(-0.18, 0.62, reflectedWorld.y);
    vec3 reflection = mix(u_groundAmbient * 0.68, u_skyAmbient * 1.38, skyReflection);
    float horizonReflection = pow(1.0 - abs(reflectedWorld.y), 3.0);
    reflection += u_materialSoft * (0.045 + horizonReflection * 0.10);

    // Screen Space Reflection (SSR) for High/Ultra tier
    if (u_qualityTier >= 2) {
        vec3 rayDirView = reflect(-V, N);
        vec4 ssrHit = TraceSSR(positionView, rayDirView, fluidDistance);
        if (ssrHit.a > 0.0) {
            reflection = mix(reflection, ssrHit.rgb, ssrHit.a);
        }
    }

    vec3 localReflectionFill = mix(refractedScene, u_skyAmbient, 0.42) * 0.55;
    float lowerHemisphere = 1.0 - skyReflection;
    reflection = mix(reflection, max(reflection, localReflectionFill), airborneWeight * lowerHemisphere);
    vec3 dielectricBase = mix(transmitted + inScatter, reflection, visibleFresnel);

    // Specular Highlights
    float surfaceNoise = sin(dot(worldPosition, vec3(12.3, 7.1, -9.5)) + u_time * 1.1) * 0.5 + 0.5;
    float roughness = mix(0.035, 0.075, surfaceNoise);
    vec3 sunHalf = normalize(V + L);
    float broadSunLobe = pow(max(dot(N, sunHalf), 0.0), 110.0) * 0.020;
    float sharpGlint = pow(max(dot(N, sunHalf), 0.0), 256.0) * smoothstep(0.55, 0.86, surfaceNoise) * 0.26;
    vec3 specular = u_sunColor * (WaterSpecularBRDF(N, V, L, roughness) * 1.0 + broadSunLobe + sharpGlint) * ndl;
    specular *= mix(vec3(1.0), u_materialGlow, 0.08);

    for (int i = 0; i < FLUID_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 toLight = u_pointLightPosRadius[i].xyz - positionView;
        float distanceToLight = length(toLight);
        float attenuation = clamp(1.0 - distanceToLight / max(u_pointLightPosRadius[i].w, 0.001), 0.0, 1.0);
        attenuation *= attenuation;
        if (attenuation <= 0.0) continue;
        vec3 pointL = toLight / max(distanceToLight, 0.001);
        float pointNdl = max(dot(N, pointL), 0.0);
        vec3 pointHalf = normalize(V + pointL);
        float broadPointLobe = pow(max(dot(N, pointHalf), 0.0), 44.0) * 0.070;
        specular += u_pointLightColor[i].rgb * (WaterSpecularBRDF(N, V, pointL, roughness) * 1.12 + broadPointLobe) * pointNdl * attenuation * 1.55;
    }

    // Shoreline Wetness & Edge Softening Foam
    float thicknessL = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(u_texel.x, 0.0)).r);
    float thicknessR = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(u_texel.x, 0.0)).r);
    float thicknessD = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(0.0, u_texel.y)).r);
    float thicknessU = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(0.0, u_texel.y)).r);
    float thicknessGradient = length(vec2(thicknessR - thicknessL, thicknessU - thicknessD)) * 0.5;
    
    float surfaceLaplacian = abs(-posL.z + -posR.z + -posD.z + -posU.z - 4.0 * (-positionView.z));
    float curvature = smoothstep(0.0012, 0.010, surfaceLaplacian);
    float receiverProximity = 1.0 - smoothstep(0.010, 0.070, sceneGap);
    float shoreline = smoothstep(0.004, 0.030, thicknessGradient) * receiverProximity * smoothstep(0.018, 0.080, kernelThickness);
    float crest = curvature * smoothstep(0.025, 0.085, kernelThickness) * (1.0 - smoothstep(0.16, 0.30, kernelThickness)) * 0.55 * mix(1.0, 0.16, airborneWeight);
    float foamPattern = smoothstep(0.48, 0.76, surfaceNoise);
    float foamMask = clamp(max(shoreline, crest) * foamPattern, 0.0, 0.72);
    vec3 foamColor = mix(u_materialSoft, vec3(1.0), 0.20);
    vec3 foam = foamColor * foamMask * (0.38 + 0.62 * dot(ambient, vec3(0.333333)));

    vec3 water = dielectricBase + specular + foam + rimLight;
    finalColor = vec4(water, surfaceCoverage);
}
