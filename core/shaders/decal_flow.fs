#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform float u_time;         // elapsed giây kể từ lúc decal spawn
uniform float u_flowSpeed;    // tốc độ cuộn ra ngoài tâm
uniform float u_flowStrength;  // trộn giữa texture gốc và texture đã cuộn [0,1]
uniform float u_glowIntensity; // boost HDR vùng sáng nhất (khe nứt); 0 = tắt glow

// Output fragment color
out vec4 finalColor;

void main()
{
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    vec2 dir = (dist > 0.0001) ? (centerDist / dist) : vec2(0.0);

    // Cuộn toạ độ radial ra ngoài tâm theo thời gian rồi fract về [0,1] ->
    // texture lặp lại tỏa dần ra mép (lava crawl / ripple spreading) thay vì
    // đứng yên như decal tĩnh.
    float scrollDist = fract(dist - u_time * u_flowSpeed);
    vec2 flowUV = vec2(0.5) + dir * scrollDist * 0.5;

    vec4 baseColor = texture(texture0, fragTexCoord);
    vec4 flowColor = texture(texture0, flowUV);
    vec4 texelColor = mix(baseColor, flowColor, clamp(u_flowStrength, 0.0, 1.0));

    // Glow tuỳ chọn (u_glowIntensity == 0 -> glowAmount luôn 0, không đổi gì
    // so với trước): texel nào vốn đã là vùng sáng nhất của texture (khe nứt
    // trên nền tối) được mix sang glowColor (tint đã brighten) thay vì cộng
    // dồn theo đúng tỉ lệ RGB gốc của tint — tint FIRE có kênh G/B rất thấp
    // (231,76,60), mà công thức luma BT.709 lại nặng kênh G (0.7152) nên
    // boost theo tỉ lệ RGB cũ gần như không đẩy được luma qua ngưỡng bloom
    // (PostFX bloomThreshold = 0.65, main.c).
    float luma = dot(texelColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    float glowMask = smoothstep(0.5, 0.85, luma);
    float glowAmount = clamp(glowMask * u_glowIntensity, 0.0, 1.0);
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
