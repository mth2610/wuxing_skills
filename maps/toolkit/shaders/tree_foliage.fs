#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "environment/shaders/cloud_shadow.glsl"
#include "environment/shaders/hemisphere_lighting.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor; // .rgb = albedo, .a = canopy mask (0=bark, 1=foliage)
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform sampler2D u_cloudNoise;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb;
    float canopyMask = fragColor.a;

    vec3 N = fragNormal * inversesqrt(max(dot(fragNormal, fragNormal), 1e-8));
    if (!gl_FrontFacing) N = -N;

    vec3 L = -u_lightDir * inversesqrt(max(dot(u_lightDir, u_lightDir), 1e-8));
    vec3 V = (u_viewPos - fragPosition) * inversesqrt(max(dot(u_viewPos - fragPosition, u_viewPos - fragPosition), 1e-8));

    float dynCov = 0.0;
    float shadow = (v_lightSpace.w > 0.0)
        ? MapShadowVisibilityCovLS(v_lightSpace, v_staticLightSpace, N, L, dynCov)
        : 1.0;
    float cloudVis = Environment_CloudVisibility(u_cloudNoise, fragPosition);
    float directVis = shadow * cloudVis;

    // Wrap diffuse for soft botanical canopy volume; crisp Lambert for bark
    float ndlRaw = dot(N, L);
    float ndl = mix(max(ndlRaw, 0.0), clamp((ndlRaw + 0.45) / 1.45, 0.0, 1.0), canopyMask);

    // Mie forward subsurface scattering through sunlit leaves/blossoms
    float backLit = pow(max(dot(V, -L), 0.0), 3.0) * canopyMask;
    vec3 sss = u_lightColor * (albedo * vec3(1.25, 1.40, 0.75) * (backLit * 0.42 * directVis));

    vec3 hemiRaw = Environment_HemisphereIrradiance(N, u_ambientColor);
    vec3 hemi = mix(max(hemiRaw, vec3(0.24, 0.21, 0.18)), hemiRaw, canopyMask);
    vec3 lit = albedo * (hemi + u_lightColor * (ndl * directVis)) * mix(0.88, 1.0, canopyMask) + sss;
    lit += VFXLights_Accumulate(fragPosition, N, albedo);

    finalColor = vec4(lit, 1.0);
}
