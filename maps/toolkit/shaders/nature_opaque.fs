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
    // 1. Two-sided normal orientation (V-crease fold & distance-aware terrain blend
    // are handled analytically inside GrassShade to avoid double-flattening the normal)
    float u = fragTexCoord.x;
    float absU = abs(u);
    vec3 baseNormal = gl_FrontFacing ? fragNormal : -fragNormal;

    // Anti-aliased vascular bundle (midrib) + soft parallel ridge (avoids high-frequency sin() Moire)
    float fwU = fwidth(u);
    float detailFade = 1.0 - FastRamp(0.35, 1.10, fwU);
    float midrib = 1.0 - FastRamp(0.0, 0.26, absU) * (0.09 * detailFade);
    float vascularRidge = (absU * (1.0 - absU) - 0.16) * (0.12 * detailFade * (1.0 - fragHeight * 0.5));
    vec3 albedo = fragColor.rgb * colDiffuse.rgb * (midrib + vascularRidge);

    vec3 lit = GrassShade(albedo, fragPosition, baseNormal, fragHeight,
                          u, v_lightSpace, v_staticLightSpace);

    // 2. Arch-windowed Wind-Wave Sheen (tapered to 0 at tip in VS so tips never flicker)
    float windSheen = fragColor.a;
    lit *= 1.0 + windSheen * 0.18;

    finalColor = vec4(lit, 1.0);
}