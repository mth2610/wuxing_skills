#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragFlow;

// Input uniform values
uniform sampler2D texture0;
uniform float u_time;         // time shared by the current decal pass
uniform float u_texScale;      // Scale UV cho vân nước (mặc định 1.0)
uniform int u_useFlowTex;      // có dùng texture1 (water_flow) không?
uniform sampler2D texture1;    // water_flow.png

#include "core/shaders/common/fx.glsl"

// Output fragment color
out vec4 finalColor;

void main()
{
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    vec2 dir = (dist > 0.0001) ? (centerDist / dist) : vec2(0.0);

    float scale = (u_texScale > 0.0) ? u_texScale : 1.0;
    vec2 scaledUV = fragTexCoord * scale;
    // Cuộn toạ độ radial ra ngoài tâm theo thời gian rồi fract về [0,1] ->
    // texture lặp lại tỏa dần ra mép (lava crawl / ripple spreading) thay vì
    // đứng yên như decal tĩnh.
    float scrollDist = fract(dist * scale - u_time * fragFlow.x);
    vec2 flowUV = vec2(0.5) + dir * scrollDist * 0.5;

    vec4 texelColor;
    if (u_useFlowTex > 0) {
        // Real flowmap blending!
        vec2 flowDir = texture(texture1, scaledUV).rg * 2.0 - 1.0;
        float caust = flowBlend(texture0, scaledUV * vec2(4.0, 2.0), flowDir, 0.55, 0.10, u_time);
        float caust2 = flowBlend(texture0, scaledUV * vec2(7.0, 3.5) + 0.37, flowDir, -0.35, 0.07, u_time);
        float finalCaust = caust * 0.7 + caust2 * 0.45;
        texelColor = vec4(finalCaust, finalCaust, finalCaust, finalCaust);
    } else {
        vec4 baseColor = texture(texture0, scaledUV);
        vec4 flowColor = texture(texture0, flowUV);
        texelColor = mix(baseColor, flowColor, clamp(fragFlow.y, 0.0, 1.0));
    }

    // Glow tuỳ chọn (u_glowIntensity == 0 -> glowAmount luôn 0, không đổi gì
    // so với trước): texel nào vốn đã là vùng sáng nhất của texture (khe nứt
    // trên nền tối) được mix sang glowColor (tint đã brighten) thay vì cộng
    // dồn theo đúng tỉ lệ RGB gốc của tint — tint FIRE có kênh G/B rất thấp
    // (231,76,60), mà công thức luma BT.709 lại nặng kênh G (0.7152) nên
    // boost theo tỉ lệ RGB cũ gần như không đẩy được luma qua ngưỡng bloom
    // (PostFX bloomThreshold = 0.65, main.c).
    float luma = dot(texelColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    float glowMask = smoothstep(0.5, 0.85, luma);
    float glowAmount = clamp(glowMask * fragFlow.z, 0.0, 1.0);
    vec3 glowColor = fragColor.rgb * 2.5;

    vec3 tintedColor = texelColor.rgb * fragColor.rgb;
    vec3 finalRGB = mix(tintedColor, glowColor, glowAmount);

    // Smooth radial fade-out, cùng công thức với decal.fs để tránh viền vuông.
    float edgeMask = smoothstep(0.5, 0.43, dist);

    // Alpha vùng glow được kéo lên gần 1 (không bị giảm theo tint alpha/
    // lifetime fade như phần decal còn lại) — alpha blend nhân trực tiếp
    // RGB lúc ghi framebuffer, alpha thấp sẽ kéo độ sáng sau blend xuống
    // dưới ngưỡng bloom dù finalRGB trước đó đã đủ sáng.
    float baseAlpha = texelColor.a * fragColor.a;
    float alpha = max(baseAlpha, glowAmount) * edgeMask;

    finalColor = vec4(finalRGB, alpha);
}
