#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

uniform sampler2D texture0;         // Bed diffuse texture; radial lake uses surrounding earth
uniform sampler2D u_causticTex;     // Dual-layer caustics texture

uniform float u_time;
uniform int u_bankPass; // Bank alpha carries wet-to-dry position, not opacity
uniform float u_waterHeight;        // Absolute world Y of the water surface
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

uniform float u_causticsStrength;
uniform float u_causticsScale;
uniform vec3 u_absorption;          // Beer-Lambert beta_e(R, G, B)
uniform vec3 u_deepColor;
uniform vec3 u_shallowColor;

out vec4 finalColor;

void main()
{
    // [VULKAN / SPIR-V FIX]: Tính đạo hàm không gian màn hình trong luồng đồng nhất (uniform control flow)
    // trước mọi lệnh discard và rẽ nhánh động để dùng cho textureGrad.
    vec2 xzDx = dFdx(fragPosition.xz);
    vec2 xzDy = dFdy(fragPosition.xz);

    // [TỐI ƯU OVERDRAW]: Nếu là lượt vẽ bờ hồ (u_bankPass > 0) và alpha tại đỉnh bằng 0, hủy sớm pixel
    float bankCoverage = smoothstep(0.0, 0.42, fragColor.a);
    if (u_bankPass > 0 && bankCoverage < 0.002) discard;

    // Physical water column depth directly above this bed fragment
    float waterDepth = max(0.0, u_waterHeight - fragPosition.y) * fragColor.a;

    // Broad soil variation with fine sand grains. Vertex color moves from
    // darker lake silt to pale sand at the waterline.
    vec2 uvBase = fragPosition.xz * 0.45;
    vec2 uvFine = fragPosition.xz * 2.60;
    vec2 uvMoist = fragPosition.xz * 0.85;
    vec3 soil = textureGrad(texture0, uvBase, xzDx * 0.45, xzDy * 0.45).rgb;
    vec3 grain = textureGrad(texture0, uvFine, xzDx * 2.60, xzDy * 2.60).rgb;
    vec3 bedAlbedo = mix(soil, grain, 0.24) * (0.48 + fragColor.rgb * 1.05);

    // Dark wet silt gives way gradually to the same textured dry bank.
    float soilMoisture = textureGrad(texture0, uvMoist, xzDx * 0.85, xzDy * 0.85).r;
    float wetness = smoothstep(0.0, 0.10 + (soilMoisture - 0.5) * 0.075, waterDepth);
    bedAlbedo *= mix(1.0, 0.70 + soilMoisture * 0.12, wetness);

    // Chuẩn hóa an toàn pháp tuyến và hướng sáng
    vec3 N = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 L = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    // [TỐI ƯU BĂNG THÔNG & ALU]: Tính causticFade TRƯỚC và chỉ đọc 2 lớp texture u_causticTex
    // khi độ sâu nằm trong dải có vân sáng (0.01m .. 1.35m), tiết kiệm 2 lượt đọc texture trên bờ khô & đáy sâu.
    float causticFade = smoothstep(0.01, 0.06, waterDepth) * (1.0 - smoothstep(0.72, 1.35, waterDepth));
    vec3 causticLight = vec3(0.0);
    if (causticFade * u_causticsStrength > 0.001) {
        float t = u_time * 0.72;
        // Dùng fract() cho độ trôi thời gian để tránh mất độ chính xác float32 khi game chạy lâu
        vec2 scale0 = vec2(u_causticsScale * 0.34);
        vec2 scale1 = vec2(u_causticsScale * 0.48);
        vec2 uv0 = fragPosition.xz * scale0 + fract(vec2(t * 0.038, t * 0.024));
        vec2 uv1 = fragPosition.xz * scale1 + fract(vec2(-t * 0.029, t * 0.043));
        float c0 = textureGrad(u_causticTex, uv0, xzDx * scale0, xzDy * scale0).r;
        float c1 = textureGrad(u_causticTex, uv1, xzDx * scale1, xzDy * scale1).r;

        // Thay pow(x, 1.5) bằng x * sqrt(x) để tránh lệnh SFU đắt đỏ và lỗi log2(0) trên SPIR-V
        float ridge = smoothstep(0.32, 0.78, min(c0, c1));
        float causticWave = ridge * sqrt(ridge);
        causticLight = u_lightColor * (causticWave * (u_causticsStrength * 0.82) * causticFade * max(L.y, 0.25));
    }

    // Terrain lighting on the lake bed
    float NdotL = max(dot(N, L), 0.0);
    vec3 directLight = u_lightColor * (NdotL * 0.90) + causticLight;
    vec3 ambientLight = u_ambientColor * 0.95;
    vec3 litBed = bedAlbedo * (directLight + ambientLight);

    // Beer-Lambert optical attenuation through the crystal clear water column
    vec3 toCam = u_viewPos - fragPosition;
    vec3 V = toCam * inversesqrt(max(dot(toCam, toCam), 1e-8));
    float cosV = max(V.y, 0.25);
    float opticalPath = waterDepth * (1.0 + 1.0 / cosV);
    vec3 transmittance = exp(-max(u_absorption, vec3(0.0)) * opticalPath);

    // Volume in-scattering through the shallow column (hài hòa chuyển sắc giữa u_shallowColor và u_deepColor)
    vec3 scatterTint = mix(u_shallowColor * 0.65 + u_deepColor * 0.35, u_deepColor, clamp(waterDepth * 0.90, 0.0, 1.0));
    vec3 waterScatter = scatterTint * (vec3(1.0) - transmittance) * (0.50 + 0.50 * u_ambientColor);

    // Composite: Submerged bed visible through crystal clear water
    vec3 finalRgb = litBed * transmittance + waterScatter;

    finalColor = vec4(finalRgb, u_bankPass > 0 ? bankCoverage : 1.0);
}
