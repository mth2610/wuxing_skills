#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 v_color;
in vec2 v_strandUV;
in float v_distToCam;
in vec3 v_worldPos;
in vec3 v_normal;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

out vec4 finalColor;

void main()
{
    float edgeDist = abs(v_strandUV.x);
    float longitudinal = smoothstep(0.0, 0.16, v_strandUV.y) * (1.0 - smoothstep(0.74, 1.0, v_strandUV.y) * 0.32);
    // Smooth feathery ribbon edge with early-Z cutout
    float alpha = min(1.0, 1.75 * longitudinal * (1.0 - smoothstep(0.38, 0.98, edgeDist)));
    if (alpha < 0.26)
        discard;

    // Shadow receiver
    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_lightDir);
    float shadowVis = FoliageShadowVisibility(
        v_lightSpace, v_staticLightSpace, N, L, v_worldPos, v_distToCam);

    // Cloud shadow
    float distSq = v_distToCam * v_distToCam;
    if (shadowVis > 0.001 && distSq < 900.0) {
        float cloudDistFade = FastRamp(576.0, 900.0, distSq);
        float cloudVis = mix(Environment_CloudVisibility(u_cloudNoise, v_worldPos), 0.92, cloudDistFade);
        shadowVis *= cloudVis;
    }

    // Mie backlit golden-hour silk rim on Cỏ Lau plumes (V points from fragment toward camera)
    vec3 V = normalize(u_viewPos - v_worldPos);
    float mieDot = max(dot(V, -L), 0.0);
    float mie2 = mieDot * mieDot;
    float silkRim = (1.0 - edgeDist * 0.45) * (0.35 + 0.65 * v_strandUV.y);
    vec3 backlitSilk = u_lightColor * ((mie2 * mie2 * 0.56 + mieDot * 0.18) * silkRim);

    vec3 plumeTone = mix(v_color * 0.90, v_color * 1.06, v_strandUV.y);
    vec3 lit = plumeTone * mix(0.64, 1.04, shadowVis) + backlitSilk * mix(0.42, 1.0, shadowVis);
    if (distSq < 900.0) {
        lit += VFXLights_Accumulate(v_worldPos, N, lit * 0.4);
    }

    finalColor = vec4(lit, 1.0);
}
