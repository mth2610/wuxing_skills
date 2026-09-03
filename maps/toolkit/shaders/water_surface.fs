#version 330
#include "core/shaders/common/vfx_lights.glsl"

in vec3 fragPosition;
in vec2 fragLakeCoord;
in vec2 fragWorldXZ;

uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec3 u_deepColor;
uniform vec3 u_shallowColor;
uniform vec3 u_foamColor;

out vec4 finalColor;

void main()
{
    float t = u_time * u_waveSpeed;
    vec2 d0 = vec2(0.82, 0.57);
    vec2 d1 = vec2(-0.31, 0.95);
    vec2 d2 = vec2(0.96, -0.18);
    float p0 = dot(fragWorldXZ, d0) * u_waveScale + t;
    float p1 = dot(fragWorldXZ, d1) * u_waveScale * 1.73 - t * 1.31;
    float p2 = dot(fragWorldXZ, d2) * u_waveScale * 2.61 + t * 0.73;
    vec2 slope = cos(p0) * d0 * u_waveScale * 0.50;
    slope += cos(p1) * d1 * u_waveScale * 1.73 * 0.29;
    slope += cos(p2) * d2 * u_waveScale * 2.61 * 0.16;
    slope *= u_waveHeight;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    float radial = length(fragLakeCoord);
    float depth = smoothstep(0.32, 0.94, radial);
    vec3 base = mix(u_deepColor, u_shallowColor, depth);
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float glint = pow(max(dot(normal, halfDir), 0.0), 92.0);
    float broadGlint = pow(max(dot(normal, halfDir), 0.0), 14.0) * 0.16;

    float shoreline = smoothstep(0.875, 0.985, radial);
    float broken = sin(fragWorldXZ.x * 2.7 + t * 0.8) * sin(fragWorldXZ.y * 3.1 - t * 0.6);
    float foam = shoreline * smoothstep(-0.15, 0.72, broken);

    vec3 color = base * (u_ambientColor + u_lightColor * 0.22);
    color += mix(vec3(0.02, 0.045, 0.06), u_shallowColor * 0.52, fresnel);
    color += u_lightColor * (glint * 0.78 + broadGlint);
    color = mix(color, u_foamColor, foam * 0.48);
    color += VFXLights_Accumulate(fragPosition, normal, base) * 0.65;
    finalColor = vec4(color, 1.0);
}
