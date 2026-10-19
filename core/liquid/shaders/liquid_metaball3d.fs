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
        if (sceneLinear > 0.15 && sceneLinear < 800.0) {
            float cosTheta = max(dot(rayDir, u_cameraForward), 0.1);
            float sceneRayDist = sceneLinear / cosTheta;
            tEnd = min(tEnd, sceneRayDist);
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
                // 4-step binary search refinement
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

    // Measure internal thickness for absorption
    float tThickSearch = tHit;
    float tBackHit = tEnd;
    float thickStep = (tEnd - tHit) / 6.0;
    for (int k = 0; k < 6; k++) {
        tThickSearch += thickStep;
        if (EvaluateField(rayOrigin + rayDir * tThickSearch) < u_threshold) {
            tBackHit = tThickSearch;
            break;
        }
        tBackHit = tThickSearch;
    }
    float thickness = max(tBackHit - tHit, 0.02);

    vec3 hitPos = rayOrigin + rayDir * tHit;
    vec3 normal = EvaluateNormal(hitPos);

    vec3 viewDir = -rayDir;
    float NdotV = max(dot(normal, viewDir), 0.0);

    // Fresnel
    float F0 = (u_liquidClass == 2) ? 0.78 : (u_liquidClass == 1 ? 0.06 : 0.02);
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    // Refraction
    vec3 refrDir = refract(rayDir, normal, 1.0 / max(u_ior, 1.001));
    vec2 refrUV = screenUV + refrDir.xy * (0.024 * clamp(thickness * 1.5, 0.1, 1.2));
    refrUV = clamp(refrUV, 0.002, 0.998);

    vec3 sceneCol = (u_hasSceneColor != 0) ? texture(u_sceneColor, refrUV).rgb : vec3(0.08, 0.14, 0.20);

    // Beer-Lambert absorption:
    // Water absorbs red light quickly, preserving clear cyan-blue.
    vec3 extinction = (vec3(1.0) - clamp(u_bodyColor.rgb, 0.0, 1.0)) * max(u_opacityPerMeter, 0.4);
    vec3 transmission = exp(-extinction * thickness * 2.5);
    vec3 refractedBody = sceneCol * transmission;
    refractedBody += (vec3(1.0) - transmission) * u_bodyColor.rgb * 0.12;

    // Direct lighting & specular highlight
    vec3 lightDir = normalize(u_sunDir);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotH = max(dot(normal, halfVec), 0.0);
    // Smooth dielectric water has high specular exponent for sharp, brilliant sun glint
    float specPower = mix(256.0, 32.0, clamp(u_roughness, 0.0, 1.0));
    float specular = pow(NdotH, specPower) * (NdotL > 0.0 ? 1.0 : 0.0);
    vec3 glintColor = mix(vec3(1.0), normalize(u_sunColor + vec3(0.3)), 0.35);
    vec3 specularLight = glintColor * specular * 1.5;

    // Scene contact foam
    float contactFoam = 0.0;
    if (u_hasSceneDepth != 0) {
        float sceneLinear = texture(u_sceneDepth, screenUV).r;
        if (sceneLinear > 0.15 && sceneLinear < 800.0) {
            float viewZ = (u_viewProj * vec4(hitPos, 1.0)).w;
            float distToScene = max(sceneLinear - viewZ, 0.0);
            contactFoam = 1.0 - smoothstep(0.0, 0.10, distToScene);
        }
    }

    vec3 color = vec3(0.0);
    float alpha = 1.0;

    if (u_liquidClass == 2) {
        // Conductor (Liquid metal, mercury)
        vec3 envReflection = mix(u_bodyColor.rgb * 0.45, u_glowColor.rgb, fresnel);
        color = envReflection + specularLight * u_glowColor.rgb;
        alpha = 1.0;
    } else if (u_liquidClass == 1) {
        // Emissive (Lava)
        float crust = smoothstep(0.25, 0.75, 1.0 - NdotV);
        vec3 coreEmissive = u_glowColor.rgb * (u_emission * (1.2 + 0.4 * sin(u_time * 2.5)));
        vec3 cooledCrust = u_bodyColor.rgb * 0.4;
        color = mix(coreEmissive, cooledCrust, crust) + specularLight * 0.5;
        alpha = 1.0;
    } else {
        // Dielectric (Water, Poison, Mud)
        vec3 skyReflect = mix(vec3(0.70, 0.88, 1.0), u_glowColor.rgb, 0.45);
        vec3 waterColor = mix(refractedBody, skyReflect, fresnel * 0.75);
        waterColor += specularLight;
        waterColor = mix(waterColor, u_softColor.rgb, contactFoam * 0.70);
        color = waterColor;
        alpha = 1.0;
    }

    // Write true curved surface depth
    vec4 clip = u_viewProj * vec4(hitPos, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    finalColor = vec4(color, alpha);
}
