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

    // Arch-windowed Wind-Wave Sheen (tapered to 0 at tip so tips never flicker)
    float windSheen = fragColor.a;
    lit *= 1.0 + windSheen * 0.18;

    finalColor = vec4(lit, 1.0);
}