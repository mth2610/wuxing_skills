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

const vec3 CLOUD_DARK  = vec3(0.30, 0.36, 0.50); // Deep atmospheric shadow in cloud valleys
const vec3 CLOUD_LIGHT = vec3(0.98, 0.93, 0.82); // Warm luminous sunlight crest

void main()
{
    // [VULKAN / SPIR-V FIX]: Tính toàn bộ đạo hàm không gian màn hình (dFdx/dFdy) trong luồng đồng nhất
    // (uniform control flow) TRƯỚC mọi lệnh rẽ nhánh và discard để tránh lỗi NaN/nhấp nháy mipmap trên Vulkan.
    vec2 uvDx1 = dFdx(v_flowUVs.xy);
    vec2 uvDy1 = dFdy(v_flowUVs.xy);
    vec2 uvDx2 = dFdx(v_flowUVs.zw);
    vec2 uvDy2 = dFdy(v_flowUVs.zw);
    vec2 flowFootprint = abs(uvDx1) + abs(uvDy1);

    // Tối ưu ALU: Dùng 1 lệnh inversesqrt duy nhất thay cho length() + phép chia
    float distSq = max(dot(v_viewVec, v_viewVec), 1e-8);
    float invDist = inversesqrt(distSq);
    float dist = distSq * invDist;
    vec3 viewDir = v_viewVec * invDist;

    // [TỐI ƯU VISUAL & KHỬ MOIRÉ]: Lọc mượt các bậc nhiễu vi mô theo cả khoảng cách VÀ góc nhìn dốc
    // (grazing angle abs(viewDir.y)) để triệt tiêu 100% hiện tượng sọc ngang (scanline banding) ở Tilt 20 deg
    float distWeight = 1.0 - smoothstep(45.0, 260.0, dist);
    float grazingWeight = smoothstep(0.04, 0.32, abs(viewDir.y));
    float octaveWeight = distWeight * grazingWeight;

    float n2 = (octaveWeight > 0.001) ? textureGrad(texture0, v_flowUVs.zw, uvDx2, uvDy2).r : 0.5;
    vec2 warpedFlow1 = v_flowUVs.xy + (n2 - 0.5) * (0.045 * octaveWeight);
    float n1 = textureGrad(texture0, warpedFlow1, uvDx1, uvDy1).r;

    float density = mix(n1, n1 * 0.66 + n2 * 0.34, octaveWeight);

    // Chân trời hòa nhập 100% vào ánh sáng vòm trời xa, không còn ngấn viền sắc cạnh
    float horizon = smoothstep(160.0, 620.0, dist);

    // Early out: Hủy ngay fragment nếu không đủ mật độ
    if (u_cloudBankEnabled == 0 && density < 0.38 && horizon < 0.98) discard;

    // Chuẩn hóa pháp tuyến cơ sở 1 lần duy nhất
    vec3 baseN = v_normal * inversesqrt(max(dot(v_normal, v_normal), 1e-8));

    // Perturb normal from cloud noise slope with derivative-aware step size (prevents horizontal scanlines)
    vec3 N;
    if (octaveWeight > 0.001) {
        vec2 stepUV = max(flowFootprint * 1.5, vec2(0.012));
        float nR = textureGrad(texture0, warpedFlow1 + vec2(stepUV.x, 0.0), uvDx1, uvDy1).r;
        float nU = textureGrad(texture0, warpedFlow1 + vec2(0.0, stepUV.y), uvDx1, uvDy1).r;
        float slopeScale = 3.8 * octaveWeight;
        vec3 billowNormal = vec3(-(nR - n1) * slopeScale, 1.0, -(nU - n1) * slopeScale);
        billowNormal *= inversesqrt(dot(billowNormal, billowNormal));
        vec3 mixedN = mix(baseN, billowNormal, 0.65 * octaveWeight);
        N = mixedN * inversesqrt(max(dot(mixedN, mixedN), 1e-8));
    } else {
        N = baseN;
    }

    float lLenSq = dot(lightDir, lightDir);
    vec3 L = (lLenSq > 1e-8) ? (-lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    // 3. Xử lý ánh sáng & Màu sắc
    vec3 actAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actLight   = (lightColor.a == 0.0) ? vec3(1.0) : lightColor.rgb;
    float zenithDamp = 1.0 / (1.0 + max(L.y - 0.38, 0.0) * 0.75);
    vec3 cloudSunLight = actLight * zenithDamp;

    // Bầu trời nền
    vec3 skyRay = vec3(v_viewVec.x, max(v_viewVec.y, 0.14), v_viewVec.z);
    vec3 sky = MapSkyRadiance(skyRay, L, u_skyAmbient, u_hazeColor, cloudSunLight);

    // Độ dày của mây & bóng tự thân kèm tán xạ nội (Multi-scattering ambient bounce)
    float edge = smoothstep(0.32, 0.76, density);
    float verticalAO = clamp(N.y * 0.48 + 0.52, 0.22, 1.0);
    float powderEffect = 1.0 - exp(-density * 2.4);
    float cloudShading = clamp(edge * verticalAO * mix(0.88, 1.08, powderEffect), 0.0, 1.0);
    vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, cloudShading);

    // Wrap lighting (Half-Lambert)
    float NdotL = max((dot(N, L) + 0.38) * 0.72, 0.0);

    // Forward scattering / Silver lining (thay pow(VdotL, 4.0) bằng 2 phép nhân ALU)
    float VdotL = max(dot(viewDir, L), 0.0);
    float v2 = VdotL * VdotL;
    float silverLining = (v2 * v2) * (1.0 - edge * 0.45) * 0.52;

    vec3 totalLight = actAmbient * 0.85 + cloudSunLight * (NdotL * 0.78 + silverLining);

    // Night celestial moonlight on cloud billows
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

    // 4. Làm mềm viền cắt (Soft Cutoff)
    float edgeFade = (u_cloudBankEnabled == 0) ? smoothstep(0.40, 0.52, density) : 1.0;
    float finalSkyBlend = mix(1.0, horizon, edgeFade);

    finalColor = vec4(mix(cloudColor, sky, finalSkyBlend), 1.0);
}
