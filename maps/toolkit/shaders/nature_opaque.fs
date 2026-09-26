#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform float u_grassTipSoftening;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    float tipPixels = max(fwidth(fragHeight) * 1.5, 0.012);
    vec3 lit = GrassShade(albedo, fragPosition, fragNormal, fragHeight,
                          fragTexCoord.x, v_lightSpace, v_staticLightSpace);
    // On a single-sample scene target, the last subpixel of a pointed blade
    // appears as a full-strength pixel. Lower only that final pixel's contrast;
    // keep opaque coverage and depth writes for the vegetation render path.
    float tip = 1.0 - smoothstep(0.0, tipPixels, 1.0 - fragHeight);
    // An edge-on blade also becomes a full-strength one-pixel line along its
    // body. UV.x spans [-1, 1], so its derivative estimates projected width.
    float widthPixels = 2.0 / max(fwidth(fragTexCoord.x), 0.0001);
    float thinBlade = 1.0 - smoothstep(0.75, 1.75, widthPixels);
    lit *= 1.0 - u_grassTipSoftening * (0.28 * tip + 0.20 * thinBlade);
    finalColor = vec4(lit, 1.0);
}
