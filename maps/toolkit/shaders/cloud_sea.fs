#version 330
#include "maps/toolkit/shaders/map_sky.glsl"

in vec4 v_flowUVs;
in vec3 v_viewVec;
in vec3 v_normal;

uniform vec3 u_skyAmbient, u_hazeColor;
uniform int u_cloudBankEnabled;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

const vec3 CLOUD_DARK  = vec3(0.56, 0.62, 0.72); // Soft twilight-slate shadow in cloud valleys
const vec3 CLOUD_LIGHT = vec3(0.96, 0.96, 0.98); // Natural white-cream droplet scattering crest

void main()
{
    // [VULKAN / SPIR-V FIX]: Tính toàn bộ đạo hàm không gian màn hình (dFdx/dFdy) trong luồng đồng nhất
    // (uniform control flow) TRƯỚC mọi lệnh rẽ nhánh để tránh lỗi mipmap trên Vulkan.
    vec2 uvDx1 = dFdx(v_flowUVs.xy);
    vec2 uvDy1 = dFdy(v_flowUVs.xy);
    vec2 uvDx2 = dFdx(v_flowUVs.zw);
    vec2 uvDy2 = dFdy(v_flowUVs.zw);
    vec2 flowFootprint = abs(uvDx1) + abs(uvDy1);

    float distSq = max(dot(v_viewVec, v_viewVec), 1e-8);
    float invDist = inversesqrt(distSq);
    float dist = distSq * invDist;
    vec3 viewDir = v_viewVec * invDist;

    float lLenSq = dot(lightDir, lightDir);
    vec3 L = (lLenSq > 1e-8) ? (-lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 actAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actLight   = (lightColor.a == 0.0) ? vec3(1.0) : lightColor.rgb;
    float zenithDamp = 1.0 / (1.0 + max(L.y - 0.38, 0.0) * 0.75);
    vec3 cloudSunLight = actLight * zenithDamp;

    // Bầu trời nền tại chân trời
    vec3 skyRay = vec3(v_viewVec.x, max(v_viewVec.y, 0.14), v_viewVec.z);
    vec3 sky = MapSkyRadiance(skyRay, L, u_skyAmbient, u_hazeColor, cloudSunLight);

    // Chân trời hòa nhập 100% vào ánh sáng vòm trời xa ở dist >= 540m
    float horizon = smoothstep(130.0, 540.0, dist);
    vec3 outColor = sky;

    // [TỐI ƯU BĂNG THÔNG TEXTURE]: Khi horizon >= 0.999 (dist >= 540m), mây đã hòa 100% vào bầu trời
    // nên bỏ qua toàn bộ 4 lần đọc textureGrad và tính toán tán xạ mây.
    if (horizon < 0.999) {
        float distWeight = 1.0 - smoothstep(45.0, 290.0, dist);
        float grazingWeight = smoothstep(0.03, 0.30, abs(viewDir.y));
        float octaveWeight = distWeight * grazingWeight;

        float n2 = (octaveWeight > 0.001) ? textureGrad(texture0, v_flowUVs.zw, uvDx2, uvDy2).r : 0.5;
        vec2 warpedFlow1 = v_flowUVs.xy + (n2 - 0.5) * (0.056 * octaveWeight);
        float n1 = textureGrad(texture0, warpedFlow1, uvDx1, uvDy1).r;

        float density = mix(n1, n1 * 0.62 + n2 * 0.38, octaveWeight);

        // Soft volumetric billow normal derived from multi-layer flow coordinates (zero extra texture taps)
        vec3 baseN = v_normal * inversesqrt(max(dot(v_normal, v_normal), 1e-8));
        vec3 N;
        if (octaveWeight > 0.001) {
            vec2 billowWarp = sin(warpedFlow1 * 6.2831853) * 0.32;
            vec3 billowNormal = vec3(-billowWarp.x, 1.0, -billowWarp.y);
            billowNormal *= inversesqrt(dot(billowNormal, billowNormal));
            vec3 mixedN = mix(baseN, billowNormal, 0.42 * octaveWeight);
            N = mixedN * inversesqrt(max(dot(mixedN, mixedN), 1e-8));
        } else {
            N = baseN;
        }

        float edge = smoothstep(0.26, 0.78, density);
        float verticalAO = clamp(N.y * 0.42 + 0.58, 0.36, 1.0);
        // Xấp xỉ hữu tỉ cho 1.0 - exp(-density * 2.6) để tiết kiệm hàm siêu việt exp()
        float dScaled = density * 2.6;
        float powderEffect = 1.0 - 1.0 / (1.0 + dScaled + 0.48 * dScaled * dScaled);
        float cloudShading = clamp(edge * verticalAO * mix(0.84, 1.12, powderEffect), 0.0, 1.0);
        vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, cloudShading);

        float NdotL = max((dot(N, L) + 0.38) * 0.72, 0.0);

        float VdotL = max(dot(viewDir, L), 0.0);
        float v2 = VdotL * VdotL;
        float silverLining = (v2 * v2) * (1.0 - edge * 0.42) * 0.68;

        vec3 totalLight = actAmbient * 0.82 + cloudSunLight * (NdotL * 0.84 + silverLining);

        if (L.y < 0.08) {
            vec3 moonRaw = vec3(-L.x, max(-L.y, 0.35), -L.z);
            vec3 moonDir = moonRaw * inversesqrt(max(dot(moonRaw, moonRaw), 1e-8));
            float moonNdotL = max((dot(N, moonDir) + 0.32) * 0.74, 0.0);
            float moonVdotL = max(dot(viewDir, moonDir), 0.0);
            float m2 = moonVdotL * moonVdotL;
            float moonSilver = (m2 * m2 * moonVdotL) * 0.42;
            vec3 moonLight = vec3(0.55, 0.70, 0.98) * (moonNdotL * 0.40 + moonSilver);
            totalLight += moonLight * (1.0 - smoothstep(-0.10, 0.08, L.y));
        }

        vec3 cloudColor = cloudBase * totalLight * colDiffuse.rgb;
        float edgeFade = (u_cloudBankEnabled == 0) ? smoothstep(0.40, 0.52, density) : 1.0;
        float finalSkyBlend = mix(1.0, horizon, edgeFade);
        outColor = mix(cloudColor, sky, finalSkyBlend);
    }

    // Khử sọc chuyển sắc 8-bit tại vùng chân trời biển mây bằng nhiễu IGN
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    outColor = max(outColor + (ign - 0.5) * (1.0 / 255.0), vec3(0.0));

    finalColor = vec4(outColor, 1.0);
}