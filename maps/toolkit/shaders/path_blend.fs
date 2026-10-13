#version 330

// Đợt E / E2 — VFX point lights. The paved path is what a caster actually
// stands on, so an effect that lights the grass but not the stone underfoot
// still reads as pasted on.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface space; see path_blend.vs
flat in vec3 fragTangent;
flat in vec3 fragBitangent;
flat in vec3 fragNormal;

uniform sampler2D texture0; // Texture đường
uniform sampler2D texture2; // optional tangent-space normal
uniform sampler2D texture3; // optional roughness
uniform vec2 tiling;
uniform vec4 colDiffuse;
uniform int u_hasSurfaceMaps;

// Thông số ánh sáng
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;
uniform vec3 viewPos;

out vec4 finalColor;

// --- HÀM TẠO NHIỄU VECTOR HÓA KHÔNG DÙNG SIN() (Fast ALU Value Noise) ---
// Nhanh hơn ~3x so với sin() hash và không bị lỗi độ chính xác khi tọa độ lớn
vec4 hash4(vec4 p) {
    p = fract(p * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p += dot(p, p.wzxy + 33.33);
    return fract((p.xxyz + p.yzzw) * p.zywx);
}

float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec2 u = f * f * (3.0 - 2.0 * f);

    // Tính hash cho cả 4 góc (0,0), (1,0), (0,1), (1,1) bằng toán tử vector
    vec4 h = hash4(vec4(i.x, i.y, i.x + 1.0, i.y + 1.0));
    float a = fract(h.x + h.y * 57.0);
    float b = fract(h.z + h.y * 57.0);
    float c = fract(h.x + h.w * 57.0);
    float d = fract(h.z + h.w * 57.0);

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main()
{
    // --- 1. EARLY-OUT DISCARD TRƯỚC KHI ĐỌC TEXTURE ---
    float edgeDistance = min(fragTexCoord.y, 1.0 - fragTexCoord.y);

    // Ngưỡng cắt tại coverage = 0.50 là (edgeLimit + 0.0375).
    // Với n thuộc [0, 1], ngưỡng này luôn >= 0.053. Nếu nhỏ hơn 0.052 -> chắc chắn bị cắt!
    if (edgeDistance < 0.052) discard;

    // Tính noise phục vụ cắt lề và hòa trộn đất đá
    float n = noise(fragPosition.xz * 1.42);
    float edgeLimit = 0.048 + (n - 0.5) * 0.065;

    // smoothstep(edgeLimit, edgeLimit + 0.075, edgeDistance) < 0.50 tương đương edgeDistance < edgeLimit + 0.0375
    if (edgeDistance < edgeLimit + 0.0375) discard;

    // --- 2. ĐỌC TEXTURE & CHỐNG LẶP VÂN (ANTI-TILING) ---
    vec2 tiledUV = fragTexCoord * tiling;
    vec2 rotatedUV = vec2(-tiledUV.y * 0.47 + 13.7, tiledUV.x * 0.47 + 8.3);

    vec4 texColor = texture(texture0, tiledUV);
    vec3 secondaryColor = texture(texture0, rotatedUV).rgb;
    texColor.rgb = mix(texColor.rgb, secondaryColor, 0.16);

    // --- 3. HÒA TRỘN LỀ ĐƯỜNG THEO CHIỀU SÂU KHE ĐÁ (HEIGHT-BASED WEATHERING) ---
    // Tính độ lồi lõm ước lượng từ độ sáng mặt đá để đất/rêu lấp vào khe trũng trước
    float stoneHeight = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    float edgeWear = 1.0 - smoothstep(0.06, 0.38, edgeDistance);
    // Đất lấn sâu hơn vào các rãnh tối màu giữa các viên đá
    float creviceMask = clamp(edgeWear * 1.35 - stoneHeight * 0.45 * (1.0 - edgeWear), 0.0, 1.0);

    // Pha màu đất ấm với chút rêu phong tự nhiên theo tần số nhiễu
    vec3 soilTone = vec3(0.68, 0.58, 0.44) * (0.85 + n * 0.30);
    vec3 mossTone = vec3(0.42, 0.48, 0.30) * (0.80 + n * 0.25);
    vec3 borderEarth = mix(soilTone, mossTone, smoothstep(0.55, 0.85, n));

    texColor.rgb = mix(texColor.rgb, borderEarth, creviceMask * 0.68);

    // Viền tối tiếp xúc (Edge Contact AO) ngay sát mép cắt giúp mặt đường bám chặt xuống nền đất
    float borderContactAO = smoothstep(edgeLimit + 0.0375, edgeLimit + 0.11, edgeDistance);
    texColor.rgb *= mix(0.72, 1.0, borderContactAO);

    // --- 4. NORMAL, ROUGHNESS & HIỆU ỨNG VÁT KHỐI 3D Ở MÉP ĐƯỜNG (CURB BEVEL) ---
    vec3 normal = fragNormal; // Đã được chuẩn hóa từ flat varying ở Vertex Shader
    float roughness = 0.82;

    if (u_hasSurfaceMaps != 0) {
        vec3 n1 = texture(texture2, tiledUV).xyz * 2.0 - 1.0;
        vec3 n2 = texture(texture2, rotatedUV).xyz * 2.0 - 1.0;
        vec3 tangentNormal = mix(n1, n2, 0.16);

        normal = normalize(fragTangent * tangentNormal.x
                         + fragBitangent * tangentNormal.y
                         + fragNormal * tangentNormal.z);

        float r1 = texture(texture3, tiledUV).r;
        float r2 = texture(texture3, rotatedUV).r;
        roughness = clamp(mix(r1, r2, 0.16), 0.22, 1.0);
    }

    // Đất bám ở rìa đường có độ nhám cao hơn mặt đá
    roughness = mix(roughness, 0.92, creviceMask * 0.65);

    // Tạo độ vát 3D nhẹ ở mép đá (Bevel Normal) giúp rìa đường bắt nắng/đổ bóng có khối
    float bevelStrength = (1.0 - borderContactAO) * 0.35;
    float edgeSign = (fragTexCoord.y < 0.5) ? -1.0 : 1.0;
    normal = normalize(normal + fragBitangent * (edgeSign * bevelStrength));

    // --- 5. TÍNH TOÁN ÁNH SÁNG & BÓNG ĐỔ (ALU OPTIMIZED) ---
    // Hợp nhất kiểm tra độ dài và chuẩn hóa lightDir (tiết kiệm 1 lệnh sqrt)
    float lightLenSq = dot(lightDir, lightDir);
    vec3 light = (lightLenSq > 0.01) ? (-lightDir * inversesqrt(lightLenSq)) : vec3(0.0, 1.0, 0.0);

    float NdotL = max(dot(normal, light), 0.0);
    vec3 actualAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actualLight   = (lightColor.a == 0.0)   ? vec3(1.0) : lightColor.rgb;

    float shadow = MapShadowVisibility(fragPosition, normal, light);

    // Ánh sáng khuếch tán có độ chuyển mềm nhẹ ở vùng chập tối (micro-wrap cho đá tự nhiên)
    float diffuseTerm = NdotL * shadow;
    vec3 totalLight = actualAmbient + actualLight * diffuseTerm;

    vec3 baseAlbedo = texColor.rgb * colDiffuse.rgb;
    vec3 pathLit = baseAlbedo * totalLight;

    // --- 6. SPECULAR GGX XẤP XỈ (KHÔNG DÙNG POW) & VFX POINT LIGHTS ---
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 halfDir = normalize(light + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float NdotH2 = NdotH * NdotH;

    // Schlick-GGX rational approximation thay thế pow(NdotH, mix(54.0, 10.0, roughness))
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    float specDistribution = a2 / (max(denom * denom, 1e-4) * 3.14159);

    // Khử lấp lánh ở vùng khe tối (Crevice Specular Occlusion)
    float specOcclusion = clamp(stoneHeight * 1.25, 0.25, 1.0) * borderContactAO;
    float specular = min(specDistribution * 0.15, 2.5) * (1.0 - roughness) * specOcclusion;

    pathLit += actualLight * (specular * NdotL * shadow);
    pathLit += VFXLights_AccumulateFlat(fragPosition, baseAlbedo);

    finalColor = vec4(pathLit, 1.0);
}