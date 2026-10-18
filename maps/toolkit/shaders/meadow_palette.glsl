// Linear RGB counterpart of meadow_palette.h. Habitat comes from ecology A.
vec3 MeadowCanopyColor(float habitat)
{
    // 3 botanical bands: deep mossy olive (0.0 .. 0.35), lush emerald clover (0.35 .. 0.75), sunlit golden meadow (0.75 .. 1.0)
    vec3 deepMoss   = vec3(0.24, 0.40, 0.13);
    vec3 lushClover = vec3(0.32, 0.50, 0.16);
    vec3 sunlitGold = vec3(0.44, 0.56, 0.22);
    vec3 color = mix(deepMoss, lushClover, smoothstep(0.20, 0.60, habitat));
    return mix(color, sunlitGold, smoothstep(0.55, 0.90, habitat));
}