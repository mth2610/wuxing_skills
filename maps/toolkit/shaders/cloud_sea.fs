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

const vec3 CLOUD_DARK  = vec3(0.38, 0.46, 0.58); // Morning atmospheric shadow
const vec3 CLOUD_LIGHT = vec3(0.96, 0.90, 0.80); // Warm morning sunlight crest

void main()
{
    // 1. Lấy mẫu lớp nhiễu phụ trước
    float n2 = texture(texture0, v_flowUVs.zw).r;
    
    // VISUAL UPGRADE: Dùng n2 làm lệch nhẹ (Domain Warp) tọa độ lớp chính
    vec2 warpedFlow1 = v_flowUVs.xy + (n2 - 0.5) * 0.04;
    float n1 = texture(texture0, warpedFlow1).r;

    // Kết hợp mật độ mây
    float density = n1 * 0.6 + n2 * 0.4;

    // 2. Tối ưu khoảng cách và vector hướng nhìn
    float dist = length(v_viewVec);
    float horizon = smoothstep(200.0, 480.0, dist) * 0.75;

    // Early out: Hủy ngay fragment nếu không đủ mật độ
    if (u_cloudBankEnabled == 0 && density < 0.38 && horizon < 0.98) discard;

    // Chuẩn hóa các vector
    vec3 viewDir = v_viewVec / max(dist, 0.0001);
    
    // Perturb normal from cloud noise slope for true 3D rolling cloud billows
    float nR = texture(texture0, warpedFlow1 + vec2(0.006, 0.0)).r;
    float nU = texture(texture0, warpedFlow1 + vec2(0.0, 0.006)).r;
    vec3 billowNormal = normalize(vec3(-(nR - n1) * 4.2, 1.0, -(nU - n1) * 4.2));
    vec3 N = normalize(mix(normalize(v_normal), billowNormal, 0.72));
    vec3 L = normalize(-lightDir);

    // 3. Xử lý ánh sáng & Màu sắc
    vec3 actAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actLight   = (lightColor.a == 0.0) ? vec3(1.0) : lightColor.rgb;
    float zenithDamp = 1.0 / (1.0 + max(L.y - 0.38, 0.0) * 0.75);
    vec3 cloudSunLight = actLight * zenithDamp;

    // Bầu trời nền
    vec3 skyRay = vec3(v_viewVec.x, max(v_viewVec.y, 0.14), v_viewVec.z);
    vec3 sky = MapSkyRadiance(skyRay, L, u_skyAmbient, u_hazeColor, cloudSunLight);

    // Độ dày của mây & bóng tự thân
    float edge = smoothstep(0.35, 0.72, density);
    float verticalAO = clamp(N.y * 0.45 + 0.55, 0.25, 1.0);
    vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, edge * verticalAO);

    // Wrap lighting (Half-Lambert)
    float NdotL = max((dot(N, L) + 0.35) * 0.74, 0.0);

    // Forward scattering / Silver lining
    float VdotL = max(dot(viewDir, L), 0.0);
    float silverLining = pow(VdotL, 4.0) * (1.0 - edge * 0.6) * 0.35;

    vec3 totalLight = actAmbient * 0.85 + cloudSunLight * (NdotL * 0.78 + silverLining);
    vec3 cloudColor = cloudBase * totalLight * colDiffuse.rgb;

    // 4. Làm mềm viền cắt (Soft Cutoff)
    // Thay vì bị cắt gắt ở ngưỡng 0.42 khi discard, hòa trộn mềm viền ngoài của mây vào màu bầu trời
    float edgeFade = (u_cloudBankEnabled == 0) ? smoothstep(0.42, 0.52, density) : 1.0;
    float finalSkyBlend = mix(1.0, horizon, edgeFade);

    finalColor = vec4(mix(cloudColor, sky, finalSkyBlend), 1.0);
}