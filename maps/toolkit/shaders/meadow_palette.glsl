// Linear RGB counterpart of meadow_palette.h. Habitat comes from ecology A.
vec3 MeadowCanopyColor(float habitat)
{
    vec3 color = mix(vec3(0.34, 0.52, 0.17), vec3(0.26, 0.47, 0.15),
                     smoothstep(0.25, 0.75, habitat));
    return mix(color, vec3(0.42, 0.56, 0.20),
               1.0 - smoothstep(0.15, 0.45, habitat));
}
