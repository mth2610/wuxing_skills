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

const vec3 CLOUD_DARK  = vec3(0.72, 0.78, 0.86); // Hạ nhẹ độ sáng để tăng độ tương phản khối
const vec3 CLOUD_LIGHT = vec3(0.95, 0.97, 1.00);

void main()
{
    // 1. Lấy mẫu lớp nhiễu phụ trước
    float n2 = texture(texture0, v_flowUVs.zw).r;
    
    // VISUAL UPGRADE: Dùng n2 làm lệch nhẹ (Domain Warp) tọa độ lớp chính
    // Giúp mây có cảm giác cuộn biến thiên tự nhiên thay vì 2 mặt phẳng trượt lên nhau
    vec2 warpedFlow1 = v_flowUVs.xy + (n2 - 0.5) * 0.035;
    float n1 = texture(texture0, warpedFlow1).r;

    // Kết hợp mật độ mây
    float density = n1 * 0.6 + n2 * 0.4;

    // 2. Tối ưu khoảng cách và vector hướng nhìn (Chỉ dùng 1 lần sqrt duy nhất)
    float dist = length(v_viewVec);
    float horizon = smoothstep(60.0, 140.0, dist);

    // Early out: Hủy ngay fragment nếu không đủ mật độ
    if (u_cloudBankEnabled == 0 && density < 0.45 && horizon < 0.98) discard;

    // Chuẩn hóa các vector (chỉ tính 1 lần)
    vec3 viewDir = v_viewVec / max(dist, 0.0001); // Tương đương normalize(v_viewVec) nhưng tận dụng lại dist
    vec3 N = normalize(v_normal);
    vec3 L = normalize(-lightDir);

    // 3. Xử lý ánh sáng & Màu sắc
    vec3 actAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actLight   = (lightColor.a == 0.0) ? vec3(1.0) : lightColor.rgb;

    // Bầu trời nền (cần tính trước để hòa trộn làm mềm viền mây)
    vec3 sky = MapSkyRadiance(v_viewVec, L, u_skyAmbient, u_hazeColor, actLight);

    // VISUAL UPGRADE: Độ dày của mây & Bóng tự thân (Vertical AO)
    // Đáy mây (N.y < 0) sẽ tối hơn đỉnh mây, tạo chiều sâu khối 3D
    float edge = smoothstep(0.45, 0.68, density);
    float verticalAO = clamp(N.y * 0.35 + 0.65, 0.35, 1.0);
    vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, edge * verticalAO);

    // Wrap lighting (Half-Lambert biến thể) giúp ánh sáng lan tỏa mềm mại
    float NdotL = max((dot(N, L) + 0.4) * 0.7142857, 0.0); // Nhân 0.7142857 nhanh hơn chia 1.4

    // VISUAL UPGRADE: Hiệu ứng viền bạc (Silver Lining / Forward Scattering)
    // Khi nhìn ngược về phía mặt trời, rìa đám mây mỏng sẽ phát sáng rực rỡ
    float VdotL = max(dot(viewDir, L), 0.0);
    float silverLining = pow(VdotL, 4.0) * (1.0 - edge * 0.7) * 0.45;

    vec3 totalLight = clamp(actAmbient + actLight * (NdotL * 0.55 + silverLining), 0.0, 1.15);
    vec3 cloudColor = cloudBase * totalLight * colDiffuse.rgb;

    // 4. Làm mềm viền cắt (Soft Cutoff)
    // Thay vì bị cắt gắt ở ngưỡng 0.45 khi discard, hòa trộn mềm viền ngoài của mây vào màu bầu trời
    float edgeFade = (u_cloudBankEnabled == 0) ? smoothstep(0.45, 0.52, density) : 1.0;
    float finalSkyBlend = mix(1.0, horizon, edgeFade);

    finalColor = vec4(mix(cloudColor, sky, finalSkyBlend), 1.0);
}