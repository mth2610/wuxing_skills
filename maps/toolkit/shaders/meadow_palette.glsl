// Linear RGB counterpart of meadow_palette.h. Habitat comes from ecology A.
vec3 MeadowCanopyColor(float habitat)
{
    vec3 color = mix(vec3(0.36, 0.54, 0.15), vec3(0.28, 0.48, 0.13),
                     smoothstep(0.25, 0.75, habitat));
    return mix(color, vec3(0.44, 0.57, 0.18),
               1.0 - smoothstep(0.15, 0.45, habitat));
}
