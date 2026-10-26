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
uniform int u_stepCount;   // coarse march steps, host-derived from the smallest blob
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
uniform int u_liquidClass; // 0: dielectric (water/poison/mud), 1: emissive (lava), 2: conductor (metal)
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform vec3 u_skyAmbient;
uniform vec3 u_groundAmbient;
uniform float u_time;

/* Bitmask of blobs actually intersected by the current pixel's ray.
 * Any blob missed by the ray has distance >= r at EVERY point along the ray
 * and contributes strictly 0 to the Wyvill field and normal on the ray. */
uint g_activeMaskLo = 0u;
uint g_activeMaskHi = 0u;
int g_firstActiveBlob = 0;
int g_lastActiveBlob = -1;

bool IsBlobActiveOnRay(int i) {
    if (i < 32) return ((g_activeMaskLo >> uint(i)) & 1u) != 0u;
    return ((g_activeMaskHi >> uint(i - 32)) & 1u) != 0u;
}

// Wyvill potential field: (1 - d^2)^3 for d < 1, restricted to ray-active blobs
float EvaluateField(vec3 p) {
    float phi = 0.0;
    for (int i = g_firstActiveBlob; i <= g_lastActiveBlob; i++) {
        if (!IsBlobActiveOnRay(i)) continue;
        vec4 b = u_blobs[i];
        vec3 d = p - b.xyz;
        if (abs(d.x) >= b.w || abs(d.y) >= b.w || abs(d.z) >= b.w) continue;
        float r2 = b.w * b.w;
        float distSq = dot(d, d);
        if (distSq < r2) {
            float w = 1.0 - distSq / r2;
            phi += w * w * w;
        }
    }
    return phi;
}

// Exact analytical gradient: grad = sum( -6 * (1 - d^2)^2 * (p - c) / r^2 )
vec3 EvaluateNormal(vec3 p) {
    vec3 grad = vec3(0.0);
    for (int i = g_firstActiveBlob; i <= g_lastActiveBlob; i++) {
        if (!IsBlobActiveOnRay(i)) continue;
        vec4 b = u_blobs[i];
        vec3 d = p - b.xyz;
        if (abs(d.x) >= b.w || abs(d.y) >= b.w || abs(d.z) >= b.w) continue;
        float invR2 = 1.0 / (b.w * b.w);
        float d2 = dot(d, d) * invR2;
        if (d2 < 1.0) {
            float w = 1.0 - d2;
            grad += (-6.0 * w * w * invR2) * d;
        }
    }
    float gLen = length(grad);
    return (gLen > 1e-5) ? (-grad / gLen) : vec3(0.0, 1.0, 0.0);
}

// Lightweight 3D value noise for organic lava crust plates and poison shimmer
float Hash31(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float Noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = Hash31(i + vec3(0, 0, 0));
    float n100 = Hash31(i + vec3(1, 0, 0));
    float n010 = Hash31(i + vec3(0, 1, 0));
    float n110 = Hash31(i + vec3(1, 1, 0));
    float n001 = Hash31(i + vec3(0, 0, 1));
    float n101 = Hash31(i + vec3(1, 0, 1));
    float n011 = Hash31(i + vec3(0, 1, 1));
    float n111 = Hash31(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

vec3 FresnelSchlick3(float ndv, vec3 f0) {
    float t = clamp(1.0 - ndv, 0.0, 1.0);
    float t2 = t * t;
    return f0 + (vec3(1.0) - f0) * (t2 * t2 * t);
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

    float tStart = max(tEnter, 0.15);
    float tEnd = tExit;

    vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
    float sceneLinear = 1e20;
    if (u_hasSceneDepth != 0) {
        sceneLinear = texture(u_sceneDepth, screenUV).r;
        if (sceneLinear > 0.15 && sceneLinear < 800.0) {
            float cosTheta = max(dot(rayDir, u_cameraForward), 0.1);
            float sceneRayDist = sceneLinear / cosTheta;
            tEnd = min(tEnd, sceneRayDist);
        }
    }

    if (tStart >= tEnd) {
        discard;
    }

    /* Fast ray-sphere interval tightening + per-ray active blob bitmask construction */
    float tightEnter = tEnd;
    float tightExit = tStart;
    g_activeMaskLo = 0u;
    g_activeMaskHi = 0u;
    g_firstActiveBlob = MAX_BLOBS;
    g_lastActiveBlob = -1;

    int count = min(u_blobCount, MAX_BLOBS);
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (i >= count) break;
        vec4 b = u_blobs[i];
        vec3 toBlob = b.xyz - rayOrigin;
        float proj = dot(toBlob, rayDir);
        if (proj + b.w < tStart || proj - b.w > tEnd) continue;
        float distSq = dot(toBlob, toBlob) - proj * proj;
        float r2 = b.w * b.w;
        if (distSq < r2) {
            float halfChord = sqrt(max(r2 - distSq, 0.0));
            tightEnter = min(tightEnter, max(tStart, proj - halfChord));
            tightExit  = max(tightExit,  min(tEnd,   proj + halfChord));
            if (i < 32) g_activeMaskLo |= (1u << uint(i));
            else        g_activeMaskHi |= (1u << uint(i - 32));
            g_firstActiveBlob = min(g_firstActiveBlob, i);
            g_lastActiveBlob = max(g_lastActiveBlob, i);
        }
    }

    if (g_lastActiveBlob < g_firstActiveBlob || tightEnter >= tightExit) {
        discard;
    }

    tStart = tightEnter;
    tEnd = tightExit;

    int numSteps = clamp(u_stepCount, 8, 36);
    float stepSize = max(tEnd - tStart, 1e-5) / float(numSteps);
    float t = tStart;
    float tPrev = t;
    float phiPrev = EvaluateField(rayOrigin + rayDir * t);
    bool hit = false;
    float tHit = t;

    if (phiPrev >= u_threshold) {
        hit = true;
        tHit = tStart;
    } else {
        for (int s = 0; s < 36; s++) {
            if (s >= numSteps) break;
            t += stepSize;
            if (t > tEnd) t = tEnd;
            float phi = EvaluateField(rayOrigin + rayDir * t);
            if (phi >= u_threshold) {
                /* Initial secant guess + 4 binary refinement steps for sub-mm accuracy */
                float frac = clamp((u_threshold - phiPrev) / max(phi - phiPrev, 1e-5), 0.1, 0.9);
                float tA = tPrev;
                float tB = t;
                float tMid = mix(tA, tB, frac);
                for (int b = 0; b < 4; b++) {
                    if (EvaluateField(rayOrigin + rayDir * tMid) >= u_threshold) {
                        tB = tMid;
                    } else {
                        tA = tMid;
                    }
                    tMid = (tA + tB) * 0.5;
                }
                tHit = tMid;
                hit = true;
                break;
            }
            if (t >= tEnd) break;
            tPrev = t;
            phiPrev = phi;
        }
    }

    if (!hit) {
        discard;
    }

    vec3 hitPos = rayOrigin + rayDir * tHit;
    vec4 hitClip = u_viewProj * vec4(hitPos, 1.0);
    float intersectionFade = 1.0;
    if (u_hasSceneDepth != 0 && sceneLinear > 0.15 && sceneLinear < 800.0) {
        float depthDiff = sceneLinear - hitClip.w;
        if (depthDiff <= -0.005) discard;
        intersectionFade = smoothstep(-0.005, 0.018, depthDiff);
    }

    float thickness;
    if (u_liquidClass == 2 || u_opacityPerMeter > 36.0) {
        thickness = max(min((tEnd - tHit) * 0.25, 0.08), 0.02);
    } else {
        float tThickSearch = tHit;
        float tBackHit = tEnd;
        float thickStep = (tEnd - tHit) / 5.0;
        for (int k = 0; k < 5; k++) {
            tThickSearch += thickStep;
            if (EvaluateField(rayOrigin + rayDir * tThickSearch) < u_threshold) {
                float tA = tThickSearch - thickStep;
                float tB = tThickSearch;
                for (int b = 0; b < 2; b++) {
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
        thickness = max(tBackHit - tHit, 0.02);
    }

    vec3 normal = EvaluateNormal(hitPos);
    float mediumOpacity = 1.0 - exp(-u_opacityPerMeter * 0.20);

    vec3 waveNormal = normal;
    if (u_liquidClass == 0) {
        /* Dampen fast capillary waves on viscous/turbid liquids like mud */
        float viscosityDamp = 1.0 - smoothstep(4.0, 20.0, u_opacityPerMeter) * 0.78;
        vec3 wp = hitPos * 20.0 + vec3(u_time * 0.9, -u_time * 1.5, u_time * 1.1) * viscosityDamp;
        float w1 = sin(wp.x * 1.8 + wp.y * 2.2 + u_time * 2.6 * viscosityDamp);
        float w2 = cos(wp.z * 2.0 - wp.y * 1.6 + u_time * 2.0 * viscosityDamp);
        float w3 = sin((wp.x + wp.z) * 2.4 - u_time * 3.1 * viscosityDamp);
        vec3 capillary = vec3(
            cos(wp.y * 2.0 + u_time * 2.0) * 0.032 + w1 * 0.022,
            sin(wp.x * 1.8 + u_time * 1.8) * 0.032 + w2 * 0.022,
            cos(wp.x * 2.1 - u_time * 2.1) * 0.032 + w3 * 0.022
        );
        capillary -= normal * dot(capillary, normal);
        waveNormal = normalize(normal + capillary * (0.38 * viscosityDamp));
    }

    vec3 viewDir = -rayDir;
    float NdotV = max(dot(waveNormal, viewDir), 0.0);

    float r0 = (u_ior - 1.0) / (u_ior + 1.0);
    float dielectricF0 = clamp(r0 * r0, 0.02, 0.18);
    float F0 = (u_liquidClass == 2) ? 0.82 : dielectricF0;
    float fresnel = F0 + (1.0 - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);

    vec3 refrDir = refract(rayDir, waveNormal, 1.0 / max(u_ior, 1.001));
    vec3 exitPos = hitPos + refrDir * thickness;

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

    vec3 sceneCol = vec3(0.0);
    if (u_hasSceneColor != 0 && u_liquidClass != 2) {
        vec2 uvR = clamp(screenUV + disp * 0.985, vec2(0.002), vec2(0.998));
        vec2 uvG = clamp(screenUV + disp * 1.000, vec2(0.002), vec2(0.998));
        vec2 uvB = clamp(screenUV + disp * 1.015, vec2(0.002), vec2(0.998));
        sceneCol.r = texture(u_sceneColor, uvR).r;
        sceneCol.g = texture(u_sceneColor, uvG).g;
        sceneCol.b = texture(u_sceneColor, uvB).b;
    } else {
        sceneCol = mix(vec3(0.05, 0.08, 0.10), vec3(0.12, 0.18, 0.22), screenUV.y);
    }

    float materialPeak = max(max(u_bodyColor.r, u_bodyColor.g), max(u_bodyColor.b, 0.001));
    vec3 materialTransmission = clamp(u_bodyColor.rgb / materialPeak, vec3(0.035), vec3(1.0));
    float opticalPath = min(thickness / max(NdotV, 0.22), 0.50);
    vec3 absorption = -log(materialTransmission) / 0.20 + vec3(0.30 + u_opacityPerMeter);
    vec3 transmittance = exp(-absorption * opticalPath);
    vec3 transmittedBody = sceneCol * transmittance;

    vec3 lightDir = normalize(u_sunDir);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotL = max(dot(waveNormal, lightDir), 0.0);
    float NdotH = max(dot(waveNormal, halfVec), 0.0);

    float specPower = mix(384.0, 56.0, clamp(u_roughness, 0.0, 1.0));
    float sunSpark = pow(NdotH, specPower) * (NdotL > 0.0 ? mix(4.2, 0.9, clamp(u_roughness, 0.0, 1.0)) : 0.0);
    float sunSheen = pow(NdotH, 36.0) * (NdotL > 0.0 ? 0.38 : 0.0);
    vec3 sunSpecular = (u_sunColor + vec3(0.45)) * (sunSpark + sunSheen);

    vec3 reflDir = reflect(rayDir, waveNormal);
    float hemi = clamp(reflDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 envSky = mix(u_groundAmbient * 0.85, u_skyAmbient * 1.35, hemi);

    vec3 ambientLight = mix(u_groundAmbient, u_skyAmbient, clamp(waveNormal.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 illumination = ambientLight + u_sunColor * (0.12 + 0.30 * NdotL);
    float grazingWeight = mix(mix(0.30, 1.0, pow(1.0 - NdotV, 1.5)), 1.0, mediumOpacity);
    float suspendedScatter = mix(0.22, 1.0, mediumOpacity);
    vec3 inScatter = u_bodyColor.rgb * (vec3(1.0) - transmittance) * illumination * (grazingWeight * suspendedScatter * 0.38);
    inScatter += u_bodyColor.rgb * illumination * (0.22 + 0.78 * NdotL) * mediumOpacity * 0.82;

    float sunForward = max(dot(refrDir, -lightDir), 0.0);
    float causticLensing = pow(sunForward, 8.0) * 0.32 * (1.0 - transmittance.r) * (1.0 - mediumOpacity);
    vec3 causticLight = u_sunColor * causticLensing;

    float contactFoam = 0.0;
    if (u_hasSceneDepth != 0 && sceneLinear > 0.15 && sceneLinear < 800.0) {
        float distToScene = max(sceneLinear - hitClip.w, 0.0);
        contactFoam = (1.0 - smoothstep(0.0, 0.06, distToScene)) * (1.0 - mediumOpacity * 0.85);
    }

    vec3 color = vec3(0.0);

    if (u_liquidClass == 2) {
        /* Studio chrome reflection matching SSF conductor optics */
        float up = reflDir.y;
        float skySplit = smoothstep(-0.045, 0.045, up);
        vec3 zenith = u_skyAmbient * 1.5;
        vec3 horizonSky = u_skyAmbient * 4.0 + u_softColor.rgb * 0.08;
        vec3 skyCol = mix(horizonSky, zenith, smoothstep(0.02, 0.85, up));
        vec3 floorCol = u_groundAmbient * mix(1.15, 0.55, smoothstep(0.0, -0.75, up)) + u_skyAmbient * 0.22;
        vec3 chromeEnv = mix(floorCol, skyCol, skySplit);
        chromeEnv *= (0.82 + 0.18 * sin(up * 11.0 + 1.7) * sin(up * 4.3 - 0.6));
        float horizonBand = pow(1.0 - min(abs(up) * 7.0, 1.0), 3.0);
        chromeEnv += u_softColor.rgb * horizonBand * 1.05;
        float sunDisc = pow(max(dot(reflDir, lightDir), 0.0), 260.0);
        chromeEnv += u_sunColor * sunDisc * 2.4;
        vec3 conductorFresnel = FresnelSchlick3(NdotV, u_bodyColor.rgb);
        color = chromeEnv * conductorFresnel + sunSpecular * u_glowColor.rgb;
    } else if (u_liquidClass == 1) {
        /* Multi-scale volcanic crust plates and incandescent magma fissures */
        float emissionDepth = 1.0 - exp(-(0.693147 / 0.20) * opticalPath);
        float crustPlates = Noise3D(hitPos * 7.0 + vec3(0.0, u_time * 0.18, 0.0));
        float crustDetail = Noise3D(hitPos * 19.0 - vec3(0.0, u_time * 0.11, 0.0));
        float rimCool = smoothstep(0.25, 0.82, 1.0 - NdotV) * 0.35;
        float crustMask = clamp(smoothstep(0.38, 0.58, crustPlates * 0.78 + crustDetail * 0.22 + rimCool), 0.0, 1.0);
        float heat = emissionDepth * (1.0 - 0.90 * crustMask);
        float core = smoothstep(0.45, 0.92, heat);
        vec3 emissionColor = mix(u_bodyColor.rgb, u_glowColor.rgb, core);
        vec3 rockBase = u_bodyColor.rgb * illumination * (0.25 + 0.75 * NdotL) * 0.38;
        color = rockBase + emissionColor * heat * max(u_emission, 1.0) + sunSpecular * (0.35 * (1.0 - 0.80 * crustMask));
    } else {
        vec3 waterColor = mix(transmittedBody + inScatter + causticLight, envSky, fresnel);
        if (u_emission > 0.0) {
            /* Bioluminescent / toxic inner glow for Poison */
            float bioDepth = 1.0 - exp(-6.0 * opticalPath);
            color = waterColor + u_glowColor.rgb * (bioDepth * u_emission * (0.50 + 0.50 * (1.0 - NdotV)));
        } else {
            color = waterColor;
        }
        color += sunSpecular;
        if (contactFoam > 0.01) {
            color = mix(color, mix(u_softColor.rgb, vec3(0.94, 0.97, 1.0), 0.5), contactFoam * 0.65);
        }
    }

    float edgeCoverage = smoothstep(0.0, 0.025, thickness) * intersectionFade;
    finalColor = vec4(color, clamp(edgeCoverage, 0.0, 1.0));
}
