#version 330

// Đợt E / E2 — VFX point lights. The paved path is what a caster actually
// stands on, so an effect that lights the grass but not the stone underfoot
// still reads as pasted on.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface/view space; see path_blend.vs
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
vec4 hash4(vec4 p) {
    p = fract(p * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p += dot(p, p.wzxy + 33.33);
    return fract((p.xxyz + p.yzzw) * p.zywx);
}

float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = st - i;
    vec2 u = f * f * (3.0 - 2.0 * f);

    vec4 h = hash4(vec4(i.x, i.y, i.x + 1.0, i.y + 1.0));
    float a = fract(h.x + h.y * 57.0);
    float b = fract(h.z + h.y * 57.0);
    float c = fract(h.x + h.w * 57.0);
    float d = fract(h.z + h.w * 57.0);

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main()
{
    // [VULKAN / SPIR-V FIX]: Tính tọa độ UV và đạo hàm không gian màn hình (dFdx/dFdy)
    // trong luồng đồng nhất (uniform control flow) TRƯỚC cả 2 lệnh discard.
    vec2 safeTiling = max(tiling, vec2(1.0));
    vec2 tiledUV = fragTexCoord * safeTiling;
    vec2 uvDx = dFdx(tiledUV);
    vec2 uvDy = dFdy(tiledUV);

    // --- 1. EARLY-OUT DISCARD TRƯỚC KHI ĐỌC TEXTURE ---
    float edgeDistance = min(fragTexCoord.y, 1.0 - fragTexCoord.y);
    if (edgeDistance < 0.052) discard;

    // [BUG FIX]: Dùng tọa độ bề mặt đường (tiledUV) thay vì fragPosition.xz (vốn nằm ở View Space
    // khi vẽ qua DrawMesh), giúp đường cắt lề đường và mảng rêu không bị trôi khi xoay/di chuyển camera!
    float n = noise(tiledUV * 1.42);
    float edgeLimit = 0.048 + (n - 0.5) * 0.065;
    if (edgeDistance < edgeLimit + 0.0375) discard;

    // --- 2. ĐỌC TEXTURE & CHỐNG LẶP VÂN (ANTI-TILING) VỚI TEXTUREGRAD ---
    vec2 rotatedUV = vec2(-tiledUV.y * 0.47 + 13.7, tiledUV.x * 0.47 + 8.3);
    vec2 rotDx = vec2(-uvDx.y, uvDx.x) * 0.47;
    vec2 rotDy = vec2(-uvDy.y, uvDy.x) * 0.47;

    vec4 texColor = textureGrad(texture0, tiledUV, uvDx, uvDy);
    vec3 secondaryColor = textureGrad(texture0, rotatedUV, rotDx, rotDy).rgb;
    texColor.rgb = mix(texColor.rgb, secondaryColor, 0.16);

    // --- 3. HÒA TRỘN LỀ ĐƯỜNG THEO CHIỀU SÂU KHE ĐÁ (HEIGHT-BASED WEATHERING) ---
    float stoneHeight = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    float edgeWear = 1.0 - smoothstep(0.06, 0.38, edgeDistance);
    float creviceMask = clamp(edgeWear * 1.35 - stoneHeight * 0.45 * (1.0 - edgeWear), 0.0, 1.0);

    vec3 soilTone = vec3(0.68, 0.58, 0.44) * (0.85 + n * 0.30);
    vec3 mossTone = vec3(0.42, 0.48, 0.30) * (0.80 + n * 0.25);
    vec3 borderEarth = mix(soilTone, mossTone, smoothstep(0.55, 0.85, n));

    texColor.rgb = mix(texColor.rgb, borderEarth, creviceMask * 0.68);

    float borderContactAO = smoothstep(edgeLimit + 0.0375, edgeLimit + 0.11, edgeDistance);
    texColor.rgb *= mix(0.72, 1.0, borderContactAO);

    // --- 4. NORMAL, ROUGHNESS & HIỆU ỨNG VÁT KHỐI 3D Ở MÉP ĐƯỜNG (CURB BEVEL) ---
    vec3 normal = fragNormal;
    float roughness = 0.82;

    if (u_hasSurfaceMaps != 0) {
        vec3 n1 = textureGrad(texture2, tiledUV, uvDx, uvDy).xyz * 2.0 - 1.0;
        vec3 n2 = textureGrad(texture2, rotatedUV, rotDx, rotDy).xyz * 2.0 - 1.0;
        // Xoay ngược pháp tuyến n2.xy 90 độ cho khớp với hệ trục của rotatedUV = (-y, x)
        n2.xy = vec2(n2.y, -n2.x);
        vec3 tangentNormal = mix(n1, n2, 0.16);

        vec3 mappedN = fragTangent * tangentNormal.x
                     + fragBitangent * tangentNormal.y
                     + fragNormal * tangentNormal.z;
        normal = mappedN * inversesqrt(max(dot(mappedN, mappedN), 1e-8));

        float r1 = textureGrad(texture3, tiledUV, uvDx, uvDy).r;
        float r2 = textureGrad(texture3, rotatedUV, rotDx, rotDy).r;
        roughness = clamp(mix(r1, r2, 0.16), 0.22, 1.0);
    }

    roughness = mix(roughness, 0.92, creviceMask * 0.65);

    float bevelStrength = (1.0 - borderContactAO) * 0.35;
    float edgeSign = (fragTexCoord.y < 0.5) ? -1.0 : 1.0;
    vec3 beveledN = normal + fragBitangent * (edgeSign * bevelStrength);
    normal = beveledN * inversesqrt(max(dot(beveledN, beveledN), 1e-8));

    // --- 5. TÍNH TOÁN ÁNH SÁNG & BÓNG ĐỔ (ALU OPTIMIZED) ---
    float lightLenSq = dot(lightDir, lightDir);
    vec3 light = (lightLenSq > 0.01) ? (-lightDir * inversesqrt(lightLenSq)) : vec3(0.0, 1.0, 0.0);

    float NdotL = max(dot(normal, light), 0.0);
    vec3 actualAmbient = (ambientColor.a == 0.0) ? vec3(0.4) : ambientColor.rgb;
    vec3 actualLight   = (lightColor.a == 0.0)   ? vec3(1.0) : lightColor.rgb;

    float shadow = (NdotL > 0.0) ? MapShadowVisibility(fragPosition, normal, light) : 0.0;
    float diffuseTerm = NdotL * shadow;
    vec3 totalLight = actualAmbient + actualLight * diffuseTerm;

    vec3 baseAlbedo = texColor.rgb * colDiffuse.rgb;
    vec3 pathLit = baseAlbedo * totalLight;

    // --- 6. SPECULAR GGX XẤP XỈ (CHỈ TÍNH KHI ĐÓN NẮNG) & VFX POINT LIGHTS ---
    if (diffuseTerm > 0.001) {
        vec3 toView = viewPos - fragPosition;
        vec3 viewDir = toView * inversesqrt(max(dot(toView, toView), 1e-8));
        vec3 halfSum = light + viewDir;
        vec3 halfDir = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-6));
        float NdotH = max(dot(normal, halfDir), 0.0);
        float NdotH2 = NdotH * NdotH;

        float a = roughness * roughness;
        float a2 = a * a;
        float denom = NdotH2 * (a2 - 1.0) + 1.0;
        float specDistribution = a2 / (max(denom * denom, 1e-4) * 3.14159);

        float specOcclusion = clamp(stoneHeight * 1.25, 0.25, 1.0) * borderContactAO;
        float specular = min(specDistribution * 0.15, 2.5) * (1.0 - roughness) * specOcclusion;

        pathLit += actualLight * (specular * diffuseTerm);
    }

    pathLit += VFXLights_AccumulateFlat(fragPosition, baseAlbedo);

    finalColor = vec4(pathLit, 1.0);
}