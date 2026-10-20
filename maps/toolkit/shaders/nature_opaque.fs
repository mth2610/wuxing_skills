#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor; // .rgb = blade color, .a = windSheen
in float fragHeight;
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform float u_grassTipSoftening;

out vec4 finalColor;

void main()
{
    // Recover true edge-to-edge normalized transverse coordinate uNorm in [-1.0, +1.0]
    // Inside the tip triangle (fragHeight > 0.76), vertex u converges toward 0.0 at the apex;
    // rescaling by the tip triangle envelope eliminates the horizontal shading seam at t = 0.76!
    float tipEnvelope = max((1.0 - fragHeight) * 4.166667, 0.08); // 1.0 / (1.0 - 0.76)
    float uNorm = (fragHeight > 0.76)
        ? clamp(fragTexCoord.x / min(tipEnvelope, 1.0), -1.0, 1.0)
        : clamp(fragTexCoord.x, -1.0, 1.0);

    vec3 baseNormal = gl_FrontFacing ? fragNormal : -fragNormal;
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;

    vec3 lit = GrassShade(albedo, fragPosition, baseNormal, fragHeight,
                          uNorm, v_lightSpace, v_staticLightSpace);

    // Dynamic Wind-Wave Glint & Sheen (Ghost of Tsushima Wind Gust Waves)
    float windSheen = fragColor.a;
    if (windSheen > 0.001) {
        float archMask = smoothstep(0.12, 0.45, fragHeight) * (1.0 - smoothstep(0.72, 0.95, fragHeight));
        vec3 sheenCol = u_lightColor * vec3(1.15, 1.08, 0.68) * (windSheen * 0.32 * archMask);
        lit += sheenCol;
        lit *= 1.0 + windSheen * 0.12;
    }

    finalColor = vec4(lit, 1.0);
}