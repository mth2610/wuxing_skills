#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    finalColor = vec4(NatureShade(albedo, fragPosition, fragNormal, fragHeight), 1.0);
}
