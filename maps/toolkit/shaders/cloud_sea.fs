#version 330
#include "maps/toolkit/shaders/map_sky.glsl"

in vec2 fragTexCoord;
in vec3 fragWorldPos;
in vec3 fragNormal;
uniform vec3 u_viewPos, u_skyAmbient, u_hazeColor;
uniform int u_cloudBankEnabled;

uniform sampler2D texture0;
uniform vec2 tiling;
uniform float u_time;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

// Khai báo hằng số giúp tránh cấp phát lại bộ nhớ trên mỗi pixel
const vec3 CLOUD_DARK = vec3(0.76, 0.81, 0.88);
const vec3 CLOUD_LIGHT = vec3(0.93, 0.95, 1.00);

void main()
{
    vec2 uv = fragTexCoord * tiling;

    // Gom phép toán nhân u_time vào vector để GPU xử lý tối ưu hơn
    vec2 flow1 = uv + vec2(0.02, 0.008) * u_time;
    vec2 flow2 = uv * 1.7 + vec2(-0.015, 0.01) * u_time;

    // Lấy mẫu texture
    float density = texture(texture0, flow1).r * 0.6 + texture(texture0, flow2).r * 0.4;

    // Early out: Hủy ngay fragment nếu không đủ mật độ
    float horizon = smoothstep(60.0, 140.0, distance(fragWorldPos, u_viewPos));
    if (u_cloudBankEnabled == 0 && density < 0.45 && horizon < 0.98) discard;

    // Mọi tính toán dưới đây chỉ chạy trên các pixel CÓ MÂY
    float edge = smoothstep(0.45, 0.6, density);
    vec3 cloudBase = mix(CLOUD_DARK, CLOUD_LIGHT, edge);

    // TỐI ƯU: dot(vec3(0,1,0), light) chính là light.y
    float NdotL = max((dot(normalize(fragNormal), normalize(-lightDir)) + 0.4) / 1.4, 0.0);

    vec3 actAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actLight = (lightColor.a == 0.0) ? vec3(1.0) : lightColor.rgb;

    vec3 totalLight = clamp(actAmbient + actLight * NdotL * 0.55, 0.0, 1.05);

    vec3 sky = MapSkyRadiance(fragWorldPos - u_viewPos, normalize(-lightDir),
                              u_skyAmbient, u_hazeColor, actLight);
    finalColor = vec4(mix(cloudBase * totalLight * colDiffuse.rgb, sky, horizon), 1.0);
}
