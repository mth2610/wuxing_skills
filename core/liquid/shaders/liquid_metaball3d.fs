#version 330

in vec3 v_worldPos;
in vec2 v_texCoord;

out vec4 finalColor;

#define MAX_BLOBS 64

uniform vec4 u_blobs[MAX_BLOBS]; // xyz: world center, w: radius in meters
uniform int u_blobCount;
uniform vec3 u_boxMin;
uniform vec3 u_boxMax;
uniform float u_threshold; // default 0.50
uniform vec3 u_cameraPos;
uniform vec3 u_cameraForward;
uniform mat4 u_viewProj;
uniform vec2 u_resolution;

uniform sampler2D u_sceneColor;
uniform sampler2D u_sceneDepth;
uniform int u_hasSceneColor;
uniform int u_hasSceneDepth;

uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform vec4 u_softColor;
uniform float u_ior;
uniform float u_roughness;
uniform float u_opacityPerMeter;
uniform float u_emission;
uniform int u_liquidClass; // 0: dielectric (water/poison), 1: emissive (lava), 2: conductor (metal)
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform float u_time;

// Wyvill potential field: (1 - d^2)^3 for d < 1
float EvaluateField(vec3 p) {
    float phi = 0.0;
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (i >= u_blobCount) break;
        vec3 d = p - u_blobs[i].xyz;
        float r = u_blobs[i].w;
        float d2 = dot(d, d) / (r * r);
        if (d2 < 1.0) {
            float w = 1.0 - d2;
            phi += w * w * w;
        }
    }
    return phi;
}

// Exact analytical gradient: grad = sum( -6 * (1 - d^2)^2 * (p - c) / r^2 )
vec3 EvaluateNormal(vec3 p) {
    vec3 grad = vec3(0.0);
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (i >= u_blobCount) break;
        vec3 d = p - u_blobs[i].xyz;
        float r = u_blobs[i].w;
        float invR2 = 1.0 / (r * r);
        float d2 = dot(d, d) * invR2;
        if (d2 < 1.0) {
            float w = 1.0 - d2;
            grad += (-6.0 * w * w * invR2) * d;
        }
    }
    float gLen = length(grad);
    return (gLen > 1e-5) ? (-grad / gLen) : vec3(0.0, 1.0, 0.0);
}

// Ray-box slab intersection
bool IntersectBox(vec3 ro, vec3 rd, vec3 bMin, vec3 bMax, out float tEnter, out float tExit) {
    vec3 safeRd = mix(rd, vec3(1e-6), lessThan(abs(rd), vec3(1e-6)));
    vec3 invR = 1.0 / safeRd;
    vec3 t0 = (bMin - ro) * invR;
    vec3 t1 = (bMax - ro) * invR;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    tEnter = max(max(tmin.x, tmin.y), tmin.z);
    tExit = min(min(tmax.x, tmax.y), tmax.z);
    return (tEnter <= tExit && tExit > 0.0);
}

void main() {
    vec3 rayOrigin = u_cameraPos;
    vec3 rayDir = normalize(v_worldPos - u_cameraPos);

    float tEnter = 0.0;
    float tExit = 0.0;
    if (!IntersectBox(rayOrigin, rayDir, u_boxMin, u_boxMax, tEnter, tExit)) {
        discard;
    }

    float tStart = max(tEnter, 0.5);
    float tEnd = tExit;

    vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
    if (u_hasSceneDepth != 0) {
        float sceneLinear = texture(u_sceneDepth, screenUV).r;
        if (sceneLinear > 1.0 && sceneLinear < 800.0) {
            float cosTheta = max(dot(rayDir, u_cameraForward), 0.1);
            float sceneRayDist = sceneLinear / cosTheta;
            if (sceneRayDist > tStart) {
                tEnd = min(tEnd, sceneRayDist);
            }
        }
    }

    if (tStart >= tEnd) {
        discard;
    }

    // Raymarching search for isosurface
    int numSteps = 48;
    float stepSize = (tEnd - tStart) / float(numSteps);
    float t = tStart;
    float tPrev = t;
    float phiPrev = EvaluateField(rayOrigin + rayDir * t);
    bool hit = false;
    float tHit = t;

    if (phiPrev >= u_threshold) {
        hit = true;
        tHit = tStart;
    } else {
        for (int s = 0; s < 48; s++) {
            t += stepSize;
            if (t > tEnd) break;
            float phi = EvaluateField(rayOrigin + rayDir * t);
            if (phi >= u_threshold) {
                float tA = tPrev;
                float tB = t;
                for (int b = 0; b < 4; b++) {
                    float tMid = (tA + tB) * 0.5;
                    if (EvaluateField(rayOrigin + rayDir * tMid) >= u_threshold) {
                        tB = tMid;
                    } else {
                        tA = tMid;
                    }
                }
                tHit = tB;
                hit = true;
                break;
            }
            tPrev = t;
            phiPrev = phi;
        }
    }

    if (!hit) {
        discard;
    }

    // Measure internal thickness for absorption with bisection refinement
    float tThickSearch = tHit;
    float tBackHit = tEnd;
    float thickStep = (tEnd - tHit) / 8.0;
    for (int k = 0; k < 8; k++) {
        tThickSearch += thickStep;
        if (EvaluateField(rayOrigin + rayDir * tThickSearch) < u_threshold) {
            float tA = tThickSearch - thickStep;
            float tB = tThickSearch;
            for (int b = 0; b < 4; b++) {
                float tMid = (tA + tB) * 0.5;
                if (EvaluateField(rayOrigin + rayDir * tMid) >= u_threshold) {
                    tA = tMid;
                } else {
                    tB = tMid;
                }
            }
            tBackHit = tB;
            break;
        }
        tBackHit = tThickSearch;
    }
    float thickness = max(tBackHit - tHit, 0.02);

    vec3 hitPos = rayOrigin + rayDir * tHit;
    vec3 normal = EvaluateNormal(hitPos);

    // Procedural fluid capillary wave & ripple perturbation (high surface tension)
    vec3 waveNormal = normal;
    if (u_liquidClass == 0) { // Dielectric fluids: water, poison
        vec3 wp = hitPos * 22.0 + vec3(u_time * 0.9, -u_time * 1.6, u_time * 1.2);
        float w1 = sin(wp.x * 1.8 + wp.y * 2.2 + u_time * 2.8);
        float w2 = cos(wp.z * 2.0 - wp.y * 1.6 + u_time * 2.1);
        float w3 = sin((wp.x + wp.z) * 2.5 - u_time * 3.4);
        vec3 capillary = vec3(
            cos(wp.y * 2.0 + u_time * 2.0) * 0.035 + w1 * 0.025,
            sin(wp.x * 1.8 + u_time * 1.8) * 0.035 + w2 * 0.025,
            cos(wp.x * 2.1 - u_time * 2.1) * 0.035 + w3 * 0.025
        );
        waveNormal = normalize(normal + capillary * (0.35 + u_roughness * 1.1));
    }

    vec3 viewDir = -rayDir;
    float NdotV = max(dot(waveNormal, viewDir), 0.0);

    // Dielectric Fresnel (water F0 = 0.020, conductor F0 = 0.78, emissive F0 = 0.06)
    float F0 = (u_liquidClass == 2) ? 0.78 : (u_liquidClass == 1 ? 0.06 : 0.02);
    float fresnel = F0 + (1.0 - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);

    // Perspective-correct screen-space refraction with 3D exit ray projection
    vec3 refrDir = refract(rayDir, waveNormal, 1.0 / max(u_ior, 1.001));
    vec3 exitPos = hitPos + refrDir * thickness;

    vec4 hitClip = u_viewProj * vec4(hitPos, 1.0);
    vec4 exitClip = u_viewProj * vec4(exitPos, 1.0);
    vec2 disp = vec2(0.0);
    if (hitClip.w > 0.001 && exitClip.w > 0.001) {
        vec2 hitNDC = hitClip.xy / hitClip.w;
        vec2 exitNDC = exitClip.xy / exitClip.w;
        disp = (exitNDC - hitNDC) * 0.5;
    } else {
        disp = refrDir.xy * (0.04 * clamp(thickness, 0.1, 1.0));
    }
    disp = clamp(disp, vec2(-0.06), vec2(0.06));

    // High-fidelity chromatic dispersion across RGB channels
    vec3 sceneCol = vec3(0.0);
    if (u_hasSceneColor != 0) {
        vec2 uvR = clamp(screenUV + disp * 0.985, vec2(0.002), vec2(0.998));
        vec2 uvG = clamp(screenUV + disp * 1.000, vec2(0.002), vec2(0.998));
        vec2 uvB = clamp(screenUV + disp * 1.015, vec2(0.002), vec2(0.998));
        sceneCol.r = texture(u_sceneColor, uvR).r;
        sceneCol.g = texture(u_sceneColor, uvG).g;
        sceneCol.b = texture(u_sceneColor, uvB).b;
    } else {
        sceneCol = mix(vec3(0.05, 0.08, 0.10), vec3(0.12, 0.18, 0.22), screenUV.y);
    }

    // Physical Beer-Lambert transmission
    float materialPeak = max(max(u_bodyColor.r, u_bodyColor.g), max(u_bodyColor.b, 0.001));
    vec3 materialTransmission = clamp(u_bodyColor.rgb / materialPeak, vec3(0.035), vec3(1.0));
    // Water reference depth 0.22m (matching SSF liquid_surface.fs)
    vec3 absorption = -log(materialTransmission) / 0.22 + vec3(0.12 + u_opacityPerMeter);
    vec3 transmittance = exp(-absorption * thickness);
    vec3 transmittedBody = sceneCol * transmittance;

    if (u_opacityPerMeter > 8.0) { // Opaque muddy liquids (e.g. mud)
        float mudDiffuse = max(dot(waveNormal, normalize(u_sunDir)), 0.0) * 0.7 + 0.3;
        transmittedBody = mix(transmittedBody, u_bodyColor.rgb * mudDiffuse, 1.0 - transmittance.g);
    }

    // Direct sun specular highlights: pinpoint diamond spark + soft water sheen
    vec3 lightDir = normalize(u_sunDir);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotL = max(dot(waveNormal, lightDir), 0.0);
    float NdotH = max(dot(waveNormal, halfVec), 0.0);

    float sunSpark = pow(NdotH, 384.0) * (NdotL > 0.0 ? 4.5 : 0.0);
    float sunSheen = pow(NdotH, 48.0) * (NdotL > 0.0 ? 0.40 : 0.0);
    vec3 sunSpecular = (u_sunColor + vec3(0.6)) * (sunSpark + sunSheen);

    // Dynamic environment reflection matching current map atmosphere (sky ambient & ground bounce)
    vec3 reflDir = reflect(rayDir, waveNormal);
    float hemi = clamp(reflDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 envSky = mix(u_groundAmbient * 0.85, u_skyAmbient * 1.35, hemi);

    // Ambient illumination & subtle suspended in-scatter
    vec3 ambientLight = mix(u_groundAmbient, u_skyAmbient, clamp(waveNormal.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 illumination = ambientLight + u_sunColor * (0.10 + 0.28 * NdotL);
    float mediumOpacity = 1.0 - exp(-u_opacityPerMeter * 0.22);
    float grazingWeight = mix(mix(0.30, 1.0, pow(1.0 - NdotV, 1.5)), 1.0, mediumOpacity);
    float suspendedScatter = mix(0.15, 1.0, mediumOpacity);
    vec3 inScatter = u_bodyColor.rgb * (vec3(1.0) - transmittance) * illumination * (grazingWeight * suspendedScatter * 0.35);

    // Optical forward caustic lensing: brightens when refracted ray aligns with sun beam
    float sunForward = max(dot(refrDir, -lightDir), 0.0);
    float causticLensing = pow(sunForward, 8.0) * 0.35 * (1.0 - transmittance.r);
    vec3 causticLight = u_sunColor * causticLensing;

    // Scene contact foam only when touching geometry
    float contactFoam = 0.0;
    if (u_hasSceneDepth != 0) {
        float sceneLinear = texture(u_sceneDepth, screenUV).r;
        if (sceneLinear > 0.15 && sceneLinear < 800.0) {
            float viewZ = (u_viewProj * vec4(hitPos, 1.0)).w;
            float distToScene = max(sceneLinear - viewZ, 0.0);
            contactFoam = 1.0 - smoothstep(0.0, 0.06, distToScene);
        }
    }

    vec3 color = vec3(0.0);
    float alpha = 1.0;

    if (u_liquidClass == 2) {
        // Conductor (Liquid metal, mercury)
        vec3 metalReflection = mix(u_bodyColor.rgb * 0.75, u_glowColor.rgb, fresnel) * envSky;
        color = metalReflection + sunSpecular * u_glowColor.rgb;
        alpha = 1.0;
    } else if (u_liquidClass == 1) {
        // Emissive (Lava)
        float crust = smoothstep(0.20, 0.75, 1.0 - NdotV);
        vec3 coreEmissive = u_glowColor.rgb * (u_emission * (1.2 + 0.35 * sin(u_time * 2.5)));
        vec3 cooledCrust = u_bodyColor.rgb * 0.35;
        color = mix(coreEmissive, cooledCrust, crust) + sunSpecular * 0.4;
        alpha = 1.0;
    } else {
        // Dielectric (Water, Poison, Mud)
        vec3 waterColor = mix(transmittedBody + inScatter + causticLight, envSky, fresnel);
        waterColor += sunSpecular;
        if (contactFoam > 0.01) {
            waterColor = mix(waterColor, vec3(0.92, 0.96, 1.0), contactFoam * 0.65);
        }
        color = waterColor;
        alpha = 1.0;
    }

    // Write true curved surface depth
    vec4 clip = u_viewProj * vec4(hitPos, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    finalColor = vec4(color, alpha);
}
