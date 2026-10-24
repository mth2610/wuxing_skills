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
    float dist = length(v_viewVec);

    // [TỐI ƯU VISUAL & KHỬ MOIRÉ]: Lọc mượt các bậc nhiễu vi mô ở khoảng cách xa để
    // triệt tiêu 100% hiện tượng sọc ngang (scanline banding) ở góc nhìn dốc (grazing angle)
    float octaveWeight = 1.0 - smoothstep(60.0, 360.0, dist);
    float n2 = mix(0.5, texture(texture0, v_flowUVs.zw).r, octaveWeight);
    vec2 warpedFlow1 = v_flowUVs.xy + (n2 - 0.5) * (0.055 * octaveWeight);
    float n1 = texture(texture0, warpedFlow1).r;

    float density = mix(n1, n1 * 0.62 + n2 * 0.38, octaveWeight);

    // Chân trời hòa nhập 100% vào ánh sáng vòm trời xa, không còn ngấn viền sắc cạnh
    float horizon = smoothstep(200.0, 720.0, dist);

    // Early out: Hủy ngay fragment nếu không đủ mật độ
    if (u_cloudBankEnabled == 0 && density < 0.38 && horizon < 0.98) discard;

    // Chuẩn hóa các vector
    vec3 viewDir = v_viewVec / max(dist, 0.0001);
    
    // Perturb normal from cloud noise slope for true 3D rolling cloud billows (damped at distance)
    float nR = texture(texture0, warpedFlow1 + vec2(0.007, 0.0)).r;
    float nU = texture(texture0, warpedFlow1 + vec2(0.0, 0.007)).r;
    vec3 billowNormal = normalize(vec3(-(nR - n1) * (6.2 * octaveWeight), 1.0, -(nU - n1) * (6.2 * octaveWeight)));
    vec3 N = normalize(mix(normalize(v_normal), billowNormal, 0.78 * octaveWeight));
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
    float edge = smoothstep(0.32, 0.76, density);
    float verticalAO = clamp(N.y * 0.48 + 0.52, 0.22, 1.0);
    vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, edge * verticalAO);

    // Wrap lighting (Half-Lambert)
    float NdotL = max((dot(N, L) + 0.38) * 0.72, 0.0);

    // Forward scattering / Silver lining on cloud wave crests facing the sun
    float VdotL = max(dot(viewDir, L), 0.0);
    float silverLining = pow(VdotL, 4.0) * (1.0 - edge * 0.45) * 0.52;

    vec3 totalLight = actAmbient * 0.85 + cloudSunLight * (NdotL * 0.78 + silverLining);

    // Night celestial moonlight on cloud billows
    if (L.y < 0.08) {
        vec3 moonDir = normalize(vec3(-L.x, max(-L.y, 0.35), -L.z));
        float moonNdotL = max((dot(N, moonDir) + 0.32) * 0.74, 0.0);
        float moonVdotL = max(dot(viewDir, moonDir), 0.0);
        float moonSilver = pow(moonVdotL, 5.0) * 0.42;
        vec3 moonLight = vec3(0.55, 0.70, 0.98) * (moonNdotL * 0.40 + moonSilver);
        totalLight += moonLight * (1.0 - smoothstep(-0.10, 0.08, L.y));
    }

    vec3 cloudColor = cloudBase * totalLight * colDiffuse.rgb;

    // 4. Làm mềm viền cắt (Soft Cutoff)
    float edgeFade = (u_cloudBankEnabled == 0) ? smoothstep(0.40, 0.52, density) : 1.0;
    float finalSkyBlend = mix(1.0, horizon, edgeFade);

    finalColor = vec4(mix(cloudColor, sky, finalSkyBlend), 1.0);
}