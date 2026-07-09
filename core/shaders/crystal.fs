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
   // 5 & 8. Gộp chung tính toán Noise (TỐI ƯU CỰC MẠNH FILL RATE)
    // Thay vì gọi vnoise3D 2 lần cho crack và dissolve, ta chỉ gọi 1 lần và biến đổi toán học
    float baseNoise = 0.0;
    if (u_crack > 0.001 || u_dissolve > 0.001)
    {
        // Chỉ tính noise 3D một lần duy nhất
        baseNoise = vnoise3D(fragPosition * 8.0); 
    }

    // 5. Internal Crack Noise
    if (u_crack > 0.001)
    {
        float crackLine = smoothstep(0.3, 0.32, baseNoise) * (1.0 - smoothstep(0.34, 0.36, baseNoise));
        finalColorRGB += u_edgeColor.rgb * (crackLine * u_crack * 0.4);
    }

    // 6. Sparkling Highlights 
    if (u_sparkle > 0.001)
    {
        vec3 p = fragPosition * 22.0;
        float sparkVal = sin(p.x + u_time) * cos(p.y - u_time) * sin(p.z + u_time * 0.8);
        
        // TỐI ƯU ALU: Thay thế pow(abs(x), 16.0) bằng phép nhân chập. 
        // GPU thực thi phép nhân nhanh hơn gấp nhiều lần so với hàm pow()
        sparkVal = abs(sparkVal);
        sparkVal *= sparkVal; // ^2
        sparkVal *= sparkVal; // ^4
        sparkVal *= sparkVal; // ^8
        sparkVal *= sparkVal; // ^16
        
        finalColorRGB += vec3(2.0) * sparkVal * u_sparkle;
    }

    // 7. Additive Rim and Emission Glow
    finalColorRGB += rimGlow + (crystalBase * (u_emission * 0.4));

    // 8. Dissolve Effect & Alpha
    float alpha = clamp(u_baseColor.a + (fresnel * u_rimStrength * 0.35), 0.0, 1.0);
    
    if (u_dissolve > 0.001)
    {
        // Fake high-frequency noise từ baseNoise bằng hàm fract() cực nhẹ
        // Thay vì phải gọi vnoise3D(fragPosition * 12.0) như cũ
        float dNoise = fract(baseNoise * 1.5 + 0.1); 
        if (dNoise < u_dissolve) discard;
        
        float edge = smoothstep(u_dissolve, u_dissolve + 0.06, dNoise);
        finalColorRGB = mix(u_edgeColor.rgb * 2.5, finalColorRGB, edge);
    }

    finalColor = vec4(finalColorRGB, alpha);
}