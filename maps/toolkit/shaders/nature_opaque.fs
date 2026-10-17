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
    // 1. Cylindrical cross-section & vascular midrib + parallel venation micro-relief
    float u = fragTexCoord.x;
    float absU = abs(u);
    vec3 baseNormal = gl_FrontFacing ? fragNormal : -fragNormal;
    vec3 sideAxis = vec3(-baseNormal.z, 0.0, baseNormal.x);
    float sideLenSq = dot(sideAxis, sideAxis);
    if (sideLenSq > 1e-6) sideAxis *= inversesqrt(sideLenSq);
    vec3 curvedNormal = normalize(baseNormal + sideAxis * (u * 0.24));

    // Botanical vascular bundle (midrib) + subtle parallel leaf veins
    float midrib = 1.0 - FastRamp(0.0, 0.28, absU) * 0.08;
    float veins  = sin(absU * 18.84955) * 0.025 * (1.0 - fragHeight * 0.5);
    vec3 albedo = fragColor.rgb * colDiffuse.rgb * (midrib + veins);

    vec3 lit = GrassShade(albedo, fragPosition, curvedNormal, fragHeight,
                          u, v_lightSpace, v_staticLightSpace);

    // 2. Arch-windowed Wind-Wave Sheen (already tapered to 0 at tip in VS so tips never flicker)
    float windSheen = fragColor.a;
    lit *= 1.0 + windSheen * 0.16;

    finalColor = vec4(lit, 1.0);
}