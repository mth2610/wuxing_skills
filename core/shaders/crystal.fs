#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"

// ============================================================
// WUXING — Crystal Shader Material (FIXED: Clear & Organic)
// Backing shader for premium translucent/emissive crystals.
// ============================================================

uniform vec4  u_baseColor;
uniform vec4  u_edgeColor;
uniform float u_fresnelPower;
uniform float u_rimStrength;
uniform float u_refraction;
uniform float u_sparkle;
uniform float u_crack;
uniform float u_emission;
uniform float u_thickness;
uniform float u_dissolve;

uniform sampler2D texture1;

// Hàm Value Noise 3D dùng chung cho hiệu ứng nứt và tan biến
float vnoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    vec2 d = vec2(0.0, 1.0);
    return mix(
        mix(
            mix(hash3(i + d.xxx), hash3(i + d.yxx), f.x),
            mix(hash3(i + d.xyx), hash3(i + d.yyx), f.x),
            f.y
        ),
        mix(
            mix(hash3(i + d.xxy), hash3(i + d.yxy), f.x),
            mix(hash3(i + d.xyy), hash3(i + d.yyy), f.x),
            f.y
        ),
        f.z
    );
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    // 1. Height Gradient & Base
    vec3 crystalBase = mix(u_baseColor.rgb, u_edgeColor.rgb, clamp(fragTexCoord.y, 0.0, 1.0));

    // 2. Diffuse & Fresnel [FIX: ĐỘ TRONG]
    // Dùng kỹ thuật Wrap Lighting: nâng mức sáng tối thiểu lên 0.4. 
    // Ánh sáng sẽ "xuyên" qua mặt tối, loại bỏ bóng đen đặc ruột.
    float diffuse = mix(0.4, 1.0, calcDiffuse(normal, lightDir, 0.25));
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);
    
    vec3 litBase = crystalBase * diffuse;
    vec3 rimGlow = u_edgeColor.rgb * (fresnel * u_rimStrength * 0.6);

    // 3. Fake Refraction
    vec2 refractUV = fragPosition.xz * 1.5 + normal.xy * u_refraction;
    vec3 refractedColor = texture(texture1, refractUV).rgb;
    vec3 finalColorRGB = mix(litBase, refractedColor * litBase, u_refraction * 0.5);

    // 4. Thickness Absorption
    float thicknessFactor = mix(0.3, 1.0, fresnel);
    float absorption = 1.0 - exp2(-1.442695 * thicknessFactor * u_thickness);
    finalColorRGB = mix(finalColorRGB * 0.25, finalColorRGB, absorption);

    // 5. Internal Crack Noise
    if (u_crack > 0.001)
    {
        float cNoise = vnoise3D(fragPosition * 8.0);
        float crackLine = smoothstep(0.3, 0.32, cNoise) * (1.0 - smoothstep(0.34, 0.36, cNoise));
        finalColorRGB += u_edgeColor.rgb * (crackLine * u_crack * 0.4);
    }

    // 6. Sparkling Highlights [FIX: HẠT VUÔNG]
    // Bỏ hàm floor(). Dùng giao thoa sóng lượng giác (Sine interference) 
    // để tạo ra các đốm sáng có hình dáng mũi nhọn tự nhiên, biến thiên theo thời gian.
    if (u_sparkle > 0.001)
    {
        vec3 p = fragPosition * 22.0;
        float sparkVal = sin(p.x + u_time) * cos(p.y - u_time) * sin(p.z + u_time * 0.8);
        
        // Nâng số mũ lên số chẵn cực lớn (16.0) để triệt tiêu các dải sáng mờ,
        // chỉ chừa lại các đỉnh (peaks) sắc lẹm lấp lánh như tinh thể.
        sparkVal = pow(abs(sparkVal), 16.0); 
        
        finalColorRGB += vec3(2.0) * sparkVal * u_sparkle;
    }

    // 7. Additive Rim and Emission Glow
    finalColorRGB += rimGlow + (crystalBase * (u_emission * 0.4));

    // 8. Dissolve Effect & Alpha [FIX: ĐỘ TRONG & HẠT VUÔNG]
    // Không đẩy alpha lên mức 1.0 (đục hoàn toàn) ở phần viền nữa. 
    // Alpha sẽ dao động tự nhiên quanh mức Alpha gốc của Color.
    float alpha = clamp(u_baseColor.a + (fresnel * u_rimStrength * 0.35), 0.0, 1.0);

    if (u_dissolve > 0.001)
    {
        // Trả lại hàm Value Noise 3D hữu cơ cho vết vỡ, thay vì noise hạt cát
        float dNoise = vnoise3D(fragPosition * 12.0); 
        if (dNoise < u_dissolve) discard;
        
        float edge = smoothstep(u_dissolve, u_dissolve + 0.06, dNoise);
        finalColorRGB = mix(u_edgeColor.rgb * 2.5, finalColorRGB, edge);
    }

    finalColor = vec4(finalColorRGB, alpha);
}