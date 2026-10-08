// Linear RGB counterpart of meadow_palette.h. Habitat comes from ecology A.
vec3 MeadowCanopyColor(float habitat)
{
    vec3 color = mix(vec3(0.46, 0.51, 0.19), vec3(0.32, 0.48, 0.14),
                     smoothstep(0.25, 0.75, habitat));
    return mix(color, vec3(0.66, 0.56, 0.30),
               1.0 - smoothstep(0.15, 0.45, habitat));
}
