#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    if (u_useTexture != 0 && fragTexCoord.x >= 0.0) {
        vec4 texel = texture(texture0, fragTexCoord);
        if (texel.a < u_alphaCutoff) discard;
        float texLuma = max(dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.12);
        // Keep the atlas hue-neutral while retaining authored veins and folds.
        albedo *= mix(0.54, 1.30, pow(texLuma, 0.78));
    } else {
        // Thin stems cover very few pixels; a modest diffuse lift keeps them
        // green after tone mapping without turning them into emissive lines.
        albedo *= vec3(1.10, 1.16, 1.06);
    }
    finalColor = vec4(NatureShade(albedo, fragPosition, fragNormal, fragHeight), 1.0);
}
