#version 330
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;       // smoothed nearest fluid depth
uniform sampler2D u_thicknessTex; // dual-depth thickness, metres
uniform sampler2D u_sceneTex;
uniform sampler2D u_sceneDepthTex;
/* The RAW front capture (RGBA32F): .r depth, .g coverage, .b liquid-table
 * slot. Read unfiltered and unsmoothed on purpose — the depth filter averages
 * its taps, and an averaged material id is a different material. */
uniform sampler2D u_materialIdTex;
uniform vec2 u_texel;      // fluid reconstruction texel
uniform vec2 u_sceneTexel; // full-resolution scene texel
uniform int u_hasSceneDepth;
uniform int u_qualityTier;
// World-space radius of one reconstruction kernel (LiquidSurface_SetReconstructionRadius).
uniform float u_kernelRadius;
uniform float u_materialKernels[6];

uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform mat4 u_viewToWorld;
uniform vec3 u_sunDirectionView;  // surface -> sun, view space
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform float u_time;

#define LIQUID_MATERIAL_SLOTS 6   // == LIQUID_SURFACE_MATERIAL_SLOTS
#define LIQUID_CLASS_DIELECTRIC 0
#define LIQUID_CLASS_EMISSIVE   1
#define LIQUID_CLASS_CONDUCTOR  2
uniform vec4 u_materialBody[LIQUID_MATERIAL_SLOTS];   // rgb body, a = class
uniform vec4 u_materialGlow[LIQUID_MATERIAL_SLOTS];   // rgb glow, a = emission
uniform vec4 u_materialSoft[LIQUID_MATERIAL_SLOTS];   // rgb soft, a = crust/foam gain
uniform vec4 u_materialOptics[LIQUID_MATERIAL_SLOTS]; // x ior, y roughness, z opacity/m, w reserved

#define LIQUID_POINT_LIGHTS 4
uniform int u_pointLightCount;
uniform vec4 u_pointLightPosRadius[LIQUID_POINT_LIGHTS];
uniform vec4 u_pointLightColor[LIQUID_POINT_LIGHTS];

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inverseProjection * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

float IorToF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

vec3 FresnelSchlick(float ndv, vec3 f0) {
    float t = clamp(1.0 - ndv, 0.0, 1.0);
    float t2 = t * t;
    return f0 + (vec3(1.0) - f0) * (t2 * t2 * t);
}

// Multi-octave procedural wave generator for realistic dynamic liquid shimmer.
vec3 WaterMultiOctaveWaves(vec3 worldPosition) {
    float t = u_time * 1.2;
    vec3 p = worldPosition;
    float s1 = sin(p.x * 6.0 + t * 0.8 + sin(p.y * 4.0 + t * 0.6));
    float s2 = sin(p.y * 7.0 - t * 0.7 + sin(p.z * 5.0 + t * 0.9));
    float s3 = sin(p.z * 5.5 + t * 1.1 + sin(p.x * 6.5 - t * 0.5));
    vec2 swell = vec2(s1 + s2, s2 + s3);
    float capFade = 0.5 + 0.5 * sin(dot(p, vec3(3.7, 5.1, 4.3)) + t * 0.7);
    vec2 capillary = vec2(
        sin(p.x * 47.0 + p.y * 29.0 + t * 2.4) + cos(p.z * 41.0 + t * 1.8),
        sin(p.z * 53.0 + p.y * 33.0 + t * 2.1) + cos(p.x * 37.0 + t * 1.6));
    vec2 dh = swell * 0.016 + capillary * 0.004 * capFade;
    return vec3(-dh.x, 0.0, -dh.y);
}

#define LIQUID_REFERENCE_DEPTH_M 0.20
#define LIQUID_TURBIDITY_PER_M 0.30

float DecodeOpticalThickness(float measuredThicknessMetres) {
    return max(measuredThicknessMetres, 0.0);
}

#define LIQUID_SPECULAR_AA_VARIANCE 0.15
#define LIQUID_SPECULAR_AA_THRESHOLD 0.25
float NormalFilteredRoughness(vec3 N, float perceptualRoughness) {
    vec3 dNdx = dFdx(N);
    vec3 dNdy = dFdy(N);
    float variance = LIQUID_SPECULAR_AA_VARIANCE * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    float kernelRoughness = min(2.0 * variance, LIQUID_SPECULAR_AA_THRESHOLD);
    float alpha = perceptualRoughness * perceptualRoughness;
    float squareAlpha = clamp(alpha * alpha + kernelRoughness, 0.0, 1.0);
    return sqrt(sqrt(squareAlpha));
}

float BlinnExponentScale(float perceptualRoughness, float filteredRoughness) {
    float alpha = perceptualRoughness * perceptualRoughness;
    float filteredAlpha = filteredRoughness * filteredRoughness;
    return clamp((alpha * alpha) / max(filteredAlpha * filteredAlpha, 1e-8), 0.02, 1.0);
}

vec3 WaterSpecularBRDF(vec3 N, vec3 V, vec3 L, float roughness, vec3 f0) {
    float ndl = max(dot(N, L), 0.0);
    float ndv = max(dot(N, V), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) return vec3(0.0);
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
    vec3 F = FresnelSchlick(vdh, f0);
    return min(D * gv * gl * F / max(4.0 * ndl * ndv, 1e-4), vec3(8.0));
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

    float stepSize = 0.035;
    vec3 currentRay = rayOrigin + rayDir * stepSize;

    for (int i = 0; i < 14; i++) {
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
    float thicknessProxy = max(texture(u_thicknessTex, fragTexCoord).r, 0.0);
    if (fluidDepth >= 0.99999 && thicknessProxy <= 0.0002) discard;

    /* Fetch the 4 cardinal neighbours once and reuse them for both silhouette
     * dilation and minimum-gradient normal reconstruction. */
    float depthLeft = texture(texture0, fragTexCoord - vec2(u_texel.x, 0.0)).r;
    float depthRight = texture(texture0, fragTexCoord + vec2(u_texel.x, 0.0)).r;
    float depthDown = texture(texture0, fragTexCoord - vec2(0.0, u_texel.y)).r;
    float depthUp = texture(texture0, fragTexCoord + vec2(0.0, u_texel.y)).r;

    bool dilatedFringe = false;
    if (fluidDepth >= 0.99999) {
        dilatedFringe = true;
        fluidDepth = min(min(depthLeft, depthRight), min(depthDown, depthUp));
        if (fluidDepth >= 0.99999) discard;
    }

    /* Two-ring material identity search: check 3x3 ring first (8 taps), and only
     * expand to the outer 5x5 ring if the 3x3 ring missed. */
    vec4 identity = texture(u_materialIdTex, fragTexCoord);
    if (identity.r >= 0.99999) {
        float nearest = 1.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                if (x == 0 && y == 0) continue;
                vec4 candidate = texture(u_materialIdTex, fragTexCoord + vec2(x, y) * u_texel);
                if (candidate.r < nearest) { nearest = candidate.r; identity = candidate; }
            }
        }
        if (nearest >= 0.99999) {
            for (int y = -2; y <= 2; y++) {
                for (int x = -2; x <= 2; x++) {
                    if (abs(x) <= 1 && abs(y) <= 1) continue;
                    vec4 candidate = texture(u_materialIdTex, fragTexCoord + vec2(x, y) * u_texel);
                    if (candidate.r < nearest) { nearest = candidate.r; identity = candidate; }
                }
            }
        }
    }
    float rawSlot = identity.b;
    int slot = clamp(int(rawSlot + 0.5), 0, LIQUID_MATERIAL_SLOTS - 1);
    float kernelRadius = u_materialKernels[slot] > 0.0 ? u_materialKernels[slot] : u_kernelRadius;
    vec3 materialBody = u_materialBody[slot].rgb;
    vec3 materialGlow = u_materialGlow[slot].rgb;
    vec3 materialSoft = u_materialSoft[slot].rgb;
    int liquidClass = int(u_materialBody[slot].a + 0.5);
    float emissionStrength = u_materialGlow[slot].a;
    float crustGain = u_materialSoft[slot].a;
    float materialIor = max(u_materialOptics[slot].x, 1.0001);
    float roughnessScale = max(u_materialOptics[slot].y, 0.01);
    float extraOpacityPerM = max(u_materialOptics[slot].z, 0.0);

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

    bool hasL = depthLeft < 0.99999;
    bool hasR = depthRight < 0.99999;
    bool hasD = depthDown < 0.99999;
    bool hasU = depthUp < 0.99999;

    vec3 posL = hasL ? ReconstructViewPosition(fragTexCoord - vec2(u_texel.x, 0.0), depthLeft) : positionView;
    vec3 posR = hasR ? ReconstructViewPosition(fragTexCoord + vec2(u_texel.x, 0.0), depthRight) : positionView;
    vec3 posD = hasD ? ReconstructViewPosition(fragTexCoord - vec2(0.0, u_texel.y), depthDown) : positionView;
    vec3 posU = hasU ? ReconstructViewPosition(fragTexCoord + vec2(0.0, u_texel.y), depthUp) : positionView;

    vec3 dx1 = positionView - posL;
    vec3 dx2 = posR - positionView;
    vec3 dy1 = positionView - posD;
    vec3 dy2 = posU - positionView;

    vec3 dx = (hasL && hasR) ? (abs(dx1.z) < abs(dx2.z) ? dx1 : dx2)
            : (hasL ? dx1 : (hasR ? dx2 : vec3(positionView.x * u_texel.x, 0.0, 0.0)));
    vec3 dy = (hasD && hasU) ? (abs(dy1.z) < abs(dy2.z) ? dy1 : dy2)
            : (hasD ? dy1 : (hasU ? dy2 : vec3(0.0, positionView.y * u_texel.y, 0.0)));

    vec3 N = normalize(cross(dx, dy));
    if (dot(N, V) < 0.0) N = -N;

    float mediumOpacity = 1.0 - exp(-extraOpacityPerM * LIQUID_REFERENCE_DEPTH_M);

    // Apply multi-octave dynamic wave perturbation, damped for viscous/turbid liquids
    mat3 viewToWorld3 = mat3(u_viewToWorld);
    vec3 worldPosition = (u_viewToWorld * vec4(positionView, 1.0)).xyz;
    vec3 worldNormal = normalize(viewToWorld3 * N);
    vec3 waveSlope = WaterMultiOctaveWaves(worldPosition);
    waveSlope -= worldNormal * dot(waveSlope, worldNormal);
    float waveGain = 0.045 * mix(1.0, 0.25, mediumOpacity);
    worldNormal = normalize(worldNormal + waveSlope * waveGain);
    N = normalize(worldNormal * viewToWorld3);
    if (dot(N, V) < 0.0) N = -N;
    float ndv = clamp(dot(N, V), 0.0, 1.0);

    float kernelThickness = DecodeOpticalThickness(thicknessProxy);
    float insideCount = (hasL ? 1.0 : 0.0) + (hasR ? 1.0 : 0.0)
                      + (hasD ? 1.0 : 0.0) + (hasU ? 1.0 : 0.0);
    float maskCoverage = dilatedFringe ? (0.25 * insideCount)
                                       : (0.5 + 0.125 * insideCount);

    float thicknessCoverage = smoothstep(0.0, 2.0 * kernelRadius, kernelThickness);
    float geometricCoverage = dilatedFringe ? thicknessCoverage : 1.0;
    float surfaceCoverage = geometricCoverage * intersectionVisibility * maskCoverage;

    float sceneGap = 1.0;
    float waterColumnDepth = kernelThickness;
    if (sceneDepthAtSurface < 0.99999) {
        float depthGap = max(sceneDistanceAtSurface - fluidDistance, 0.0);
        sceneGap = depthGap;
        waterColumnDepth = min(kernelThickness, max(0.022, depthGap * 1.25));
    }

    float opticalPath = min(waterColumnDepth / max(ndv, 0.22),
                            LIQUID_REFERENCE_DEPTH_M * 2.5);
    float airborneWeight = smoothstep(0.070, 0.220, sceneGap);
    bool conductor = (liquidClass == LIQUID_CLASS_CONDUCTOR);

    // Snell Refraction (skipped for opaque conductors)
    vec3 refractedScene = vec3(0.0);
    if (!conductor) {
        vec3 incident = -V;
        vec3 refractedDirection = refract(incident, N, 1.0 / materialIor);
        vec2 incidentSlope = incident.xy / max(abs(incident.z), 0.25);
        vec2 refractedSlope = refractedDirection.xy / max(abs(refractedDirection.z), 0.25);
        float travelPixels = clamp(opticalPath * 100.0, 0.35, 13.0);
        vec2 refractOffset = (refractedSlope - incidentSlope) * travelPixels * u_sceneTexel;
        vec2 refractUV = clamp(fragTexCoord + refractOffset, u_sceneTexel, vec2(1.0) - u_sceneTexel);

        if (!SceneSampleMatchesBase(refractUV, sceneDistanceAtSurface, fluidDistance)) {
            refractUV = mix(refractUV, fragTexCoord, 0.70);
        }

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
    }

    // Beer-Lambert Volumetric Absorption
    float materialPeak = max(max(materialBody.r, materialBody.g), max(materialBody.b, 0.001));
    vec3 materialTransmission = clamp(materialBody / materialPeak, vec3(0.035), vec3(1.0));
    vec3 absorption = -log(materialTransmission) / LIQUID_REFERENCE_DEPTH_M
                    + vec3(LIQUID_TURBIDITY_PER_M + extraOpacityPerM);
    vec3 transmittance = exp(-absorption * opticalPath);

    float hemi = clamp(worldNormal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(u_groundAmbient, u_skyAmbient, hemi);
    vec3 L = normalize(u_sunDirectionView);
    float ndl = max(dot(N, L), 0.0);

    vec3 waterScatterColor = materialBody;
    vec3 illumination = ambient + u_sunColor * (0.12 + 0.30 * ndl);
    vec3 transmitted = refractedScene * transmittance;
    float illuminationEnergy = dot(illumination, vec3(0.2126, 0.7152, 0.0722));
    float backgroundScatter = 1.0 - dot(transmittance, vec3(0.2126, 0.7152, 0.0722));
    float scatterAmount = clamp(0.30 + 0.70 * backgroundScatter, 0.0, 1.0);

    float grazingWeight = mix(mix(0.32, 1.0, pow(1.0 - ndv, 1.5)), 1.0, mediumOpacity);
    float suspendedScatter = mix(0.24, 1.0, mediumOpacity);
    vec3 inScatter = waterScatterColor * scatterAmount * (0.34 + illuminationEnergy * 0.42)
                   * grazingWeight * suspendedScatter;
    inScatter += materialBody * illumination * (0.22 + 0.78 * ndl) * mediumOpacity * 0.85;
    if (liquidClass == LIQUID_CLASS_EMISSIVE) inScatter *= 0.35;

    vec3 dielectricF0 = vec3(IorToF0(materialIor));
    vec3 fresnel = FresnelSchlick(ndv, dielectricF0);
    float volumeWeight = smoothstep(0.016, 0.095, kernelThickness);
    vec3 visibleFresnel = fresnel * mix(0.045, 1.0, volumeWeight);

    float rimStrength = pow(1.0 - ndv, 2.0) * smoothstep(0.012, 0.10, kernelThickness);
    float rimGate = conductor ? 0.0 : 1.0;
    vec3 rimLight = materialGlow * rimStrength * (0.12 + 0.55 * illuminationEnergy)
                  * (0.30 + 0.70 * ndl) * rimGate;
    vec3 viewWorld = normalize(viewToWorld3 * V);
    vec3 reflectedWorld = reflect(-viewWorld, worldNormal);

    float skyReflection = conductor ? smoothstep(-0.045, 0.045, reflectedWorld.y)
                                    : smoothstep(-0.18, 0.62, reflectedWorld.y);
    float skyLuma = dot(u_skyAmbient, vec3(0.2126, 0.7152, 0.0722));
    vec3 skyReflectionColor = u_skyAmbient * 1.38 + materialGlow * 0.65 * clamp(1.0 - skyLuma, 0.0, 1.0);
    vec3 reflection;
    if (conductor) {
        float up = reflectedWorld.y;
        vec3 zenith     = u_skyAmbient * 1.5;
        vec3 horizonSky = u_skyAmbient * 4.0 + materialSoft * 0.08;
        vec3 skyColor   = mix(horizonSky, zenith, smoothstep(0.02, 0.85, up));
        vec3 floorColor = u_groundAmbient * mix(1.15, 0.55, smoothstep(0.0, -0.75, up))
                        + u_skyAmbient * 0.22;
        reflection = mix(floorColor, skyColor, skyReflection);

        float bands = 0.82 + 0.18 * sin(up * 11.0 + 1.7) * sin(up * 4.3 - 0.6);
        reflection *= bands;

        float band = pow(1.0 - min(abs(up) * 7.0, 1.0), 3.0);
        reflection += materialSoft * band * 1.05;

        vec3 sunWorld = normalize(viewToWorld3 * L);
        float sunDisc = pow(max(dot(reflectedWorld, sunWorld), 0.0), 260.0);
        reflection += u_sunColor * sunDisc * 2.4;
    } else {
        reflection = mix(u_groundAmbient * 0.68, skyReflectionColor, skyReflection);
        float horizonReflection = pow(1.0 - abs(reflectedWorld.y), 3.0);
        reflection += materialSoft * (0.045 + horizonReflection * 0.10);
    }

    float visibleFresnelLuma = max(max(visibleFresnel.r, visibleFresnel.g), visibleFresnel.b);
    float ssrWeight = conductor ? 1.0 : visibleFresnelLuma;
    if (u_qualityTier >= 2 && ssrWeight > 0.05) {
        vec3 rayDirView = reflect(-V, N);
        vec4 ssrHit = TraceSSR(positionView, rayDirView, fluidDistance);
        if (ssrHit.a > 0.0) {
            reflection = mix(reflection, ssrHit.rgb, ssrHit.a);
        }
    }

    if (!conductor) {
        vec3 localReflectionFill = mix(refractedScene, u_skyAmbient, 0.30) * 0.42;
        float lowerHemisphere = 1.0 - skyReflection;
        reflection = mix(reflection, max(reflection, localReflectionFill), airborneWeight * lowerHemisphere);
    }
    vec3 dielectricBase = mix(transmitted + inScatter, reflection, visibleFresnel);

    float surfaceNoise = fbm3(worldPosition * 2.7 + vec3(0.0, u_time * 0.35, 0.0));
    float authoredRoughness = mix(0.090, 0.150, surfaceNoise) * roughnessScale;
    float roughness = NormalFilteredRoughness(N, authoredRoughness);
    float lobeScale = BlinnExponentScale(authoredRoughness, roughness);
    vec3 sunHalf = normalize(V + L);
    float sunNdh = max(dot(N, sunHalf), 0.0);

    float broadExponent = 48.0 * lobeScale;
    float glintExponent = 96.0 * lobeScale;
    float broadSunLobe = pow(sunNdh, broadExponent) * 0.020 * ((broadExponent + 2.0) / 50.0);
    float sharpGlint = pow(sunNdh, glintExponent) * 0.22 * ((glintExponent + 2.0) / 98.0)
                     * smoothstep(0.55, 0.86, surfaceNoise);
    vec3 specularF0 = dielectricF0;
    float blinnGate = 1.0;
    if (conductor) { specularF0 = materialBody; blinnGate = 0.0; }
    vec3 specular = u_sunColor * (WaterSpecularBRDF(N, V, L, roughness, specularF0)
                                  + vec3((broadSunLobe + sharpGlint) * blinnGate)) * ndl;
    specular *= mix(vec3(1.0), materialGlow, 0.08);

    for (int i = 0; i < LIQUID_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 toLight = u_pointLightPosRadius[i].xyz - positionView;
        float distanceToLight = length(toLight);
        float attenuation = clamp(1.0 - distanceToLight / max(u_pointLightPosRadius[i].w, 0.001), 0.0, 1.0);
        attenuation *= attenuation;
        if (attenuation <= 0.0) continue;
        vec3 pointL = toLight / max(distanceToLight, 0.001);
        float pointNdl = max(dot(N, pointL), 0.0);
        vec3 pointHalf = normalize(V + pointL);
        float pointExponent = 44.0 * lobeScale;
        float broadPointLobe = pow(max(dot(N, pointHalf), 0.0), pointExponent)
                             * 0.070 * ((pointExponent + 2.0) / 46.0);
        specular += u_pointLightColor[i].rgb * (WaterSpecularBRDF(N, V, pointL, roughness, specularF0) * 1.12
                                                + vec3(broadPointLobe * blinnGate)) * pointNdl * attenuation * 1.55;
    }

    // Shoreline Wetness & Edge Softening Foam
    float thicknessL = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(u_texel.x, 0.0)).r);
    float thicknessR = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(u_texel.x, 0.0)).r);
    float thicknessD = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord - vec2(0.0, u_texel.y)).r);
    float thicknessU = DecodeOpticalThickness(texture(u_thicknessTex, fragTexCoord + vec2(0.0, u_texel.y)).r);
    float thicknessGradient = length(vec2(thicknessR - thicknessL, thicknessU - thicknessD)) * 0.5;

    /* Guard surfaceLaplacian against missing silhouette neighbours: if one side
     * is missing, mirror the opposite valid side so linear slopes cancel to 0
     * instead of spiking to +1000m (far plane) at the silhouette. */
    float zC = -positionView.z;
    float zL = hasL ? -posL.z : (hasR ? (2.0 * zC - (-posR.z)) : zC);
    float zR = hasR ? -posR.z : (hasL ? (2.0 * zC - (-posL.z)) : zC);
    float zD = hasD ? -posD.z : (hasU ? (2.0 * zC - (-posU.z)) : zC);
    float zU = hasU ? -posU.z : (hasD ? (2.0 * zC - (-posD.z)) : zC);
    float surfaceLaplacian = abs(zL + zR + zD + zU - 4.0 * zC);

    float curvature = smoothstep(0.0012, 0.010, surfaceLaplacian);
    float receiverProximity = 1.0 - smoothstep(0.010, 0.070, sceneGap);
    float shoreline = smoothstep(0.004, 0.030, thicknessGradient) * receiverProximity * smoothstep(0.018, 0.080, kernelThickness);
    float crest = curvature * smoothstep(0.025, 0.085, kernelThickness) * (1.0 - smoothstep(0.16, 0.30, kernelThickness)) * 0.55 * mix(1.0, 0.16, airborneWeight);
    float foamPattern = smoothstep(0.48, 0.76, surfaceNoise);
    float foamMask = clamp(max(shoreline, crest) * foamPattern, 0.0, 0.72);
    vec3 foamColor = mix(materialSoft, vec3(1.0), 0.20);
    vec3 foam = foamColor * foamMask * (0.38 + 0.62 * dot(ambient, vec3(0.333333)))
              * (liquidClass == LIQUID_CLASS_EMISSIVE ? 0.0 : crustGain);

    vec3 emission = vec3(0.0);
    float crustMask = 0.0;
    if (liquidClass == LIQUID_CLASS_EMISSIVE && emissionStrength > 0.0) {
        float emissionDepth = 1.0 - exp(-(0.693147 / LIQUID_REFERENCE_DEPTH_M) * opticalPath);
        float crustPlates = fbm3(worldPosition * 7.0 + vec3(0.0, u_time * 0.18, 0.0));
        float crustDetail = fbm3(worldPosition * 19.0 - vec3(0.0, u_time * 0.11, 0.0));
        crustMask = smoothstep(0.40, 0.57, crustPlates * 0.78 + crustDetail * 0.22) * crustGain;

        float heat = emissionDepth * (1.0 - 0.92 * crustMask);
        float core = smoothstep(0.48, 0.94, heat);
        vec3 emissionColor = mix(materialBody, materialGlow, core);
        emission = emissionColor * heat * emissionStrength;
    } else if (liquidClass == LIQUID_CLASS_DIELECTRIC && emissionStrength > 0.0) {
        /* Volumetric bioluminescent / toxic glow for Poison and glowing dielectrics */
        float bioDepth = 1.0 - exp(-(1.15 / LIQUID_REFERENCE_DEPTH_M) * opticalPath);
        float bioPulse = 0.88 + 0.12 * sin(u_time * 2.6 + surfaceNoise * 4.5);
        emission = materialGlow * (bioDepth * emissionStrength * bioPulse * (0.45 + 0.55 * (1.0 - ndv)));
    }

    vec3 base = dielectricBase;
    if (conductor) {
        vec3 conductorFresnel = FresnelSchlick(ndv, specularF0);
        base = reflection * conductorFresnel;
    }

    if (liquidClass == LIQUID_CLASS_EMISSIVE) specular *= (1.0 - 0.80 * crustMask);

    vec3 water = base + specular + foam + rimLight + emission;
    finalColor = VFX_ResolveBody(water, 1.0, surfaceCoverage);
}
