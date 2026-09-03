#version 330
#include "core/shaders/common/vfx_lights.glsl"

in vec3 fragPosition;
in vec2 fragLakeCoord;
in vec2 fragWorldXZ;

uniform sampler2D texture0;
uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;
uniform float u_detailScale;
uniform float u_detailStrength;
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
    slope *= u_waveHeight * 1.85;
    vec2 d3 = vec2(0.43, -0.90);
    float p3 = dot(fragWorldXZ, d3) * u_waveScale * 4.70 - t * 1.82;
    slope += cos(p3) * d3 * 0.032;

    vec2 detailUv0 = fragWorldXZ * u_detailScale + vec2(t * 0.011, -t * 0.007);
    vec2 detailUv1 = vec2(-fragWorldXZ.y, fragWorldXZ.x) * u_detailScale * 0.63
                   + vec2(-t * 0.006, t * 0.009);
    float detail0 = texture(texture0, detailUv0).r;
    float detail1 = texture(texture0, detailUv1).r;
    float detailX = texture(texture0, detailUv0 + vec2(0.006, 0.0)).r - detail0;
    float detailZ = texture(texture0, detailUv0 + vec2(0.0, 0.006)).r - detail0;
    float detailX1 = texture(texture0, detailUv1 + vec2(0.006, 0.0)).r - detail1;
    float detailZ1 = texture(texture0, detailUv1 + vec2(0.0, 0.006)).r - detail1;
    slope += (vec2(detailX, detailZ) * 0.72
           + vec2(detailZ1, -detailX1) * 0.28) * u_detailStrength * 2.40;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    float radial = length(fragLakeCoord);
    float depth = smoothstep(0.32, 0.94, radial);
    vec3 base = mix(u_deepColor, u_shallowColor, depth);
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float glint = pow(max(dot(normal, halfDir), 0.0), 92.0);
    float broadGlint = pow(max(dot(normal, halfDir), 0.0), 14.0) * 0.16;
    float roughGlint = pow(max(dot(normal, halfDir), 0.0), 28.0) * 0.12;

    float shallows = smoothstep(0.72, 0.985, radial);
    float shoreline = smoothstep(0.952, 0.996, radial);
    float broken = sin(fragWorldXZ.x * 3.7 + t * 0.45)
                 + sin(fragWorldXZ.y * 4.3 - t * 0.38);
    float foam = shoreline * smoothstep(1.25, 1.82, broken) * 0.16;

    vec3 reflectionDir = reflect(-viewDir, normal);
    float skyFacing = smoothstep(-0.10, 0.88, reflectionDir.y);
    vec3 reflectedSky = mix(u_ambientColor * 0.48, u_lightColor * 0.62, skyFacing);
    vec3 color = base * (0.46 + u_ambientColor * 0.72 + u_lightColor * 0.12);
    color += reflectedSky * (0.18 + fresnel * 0.48);
    color = mix(color, u_shallowColor * 0.84, shallows * 0.34);
    float rippleLight = sin(fragWorldXZ.x * 1.36 + t * 0.62)
                      * sin(fragWorldXZ.y * 1.71 - t * 0.51);
    color += reflectedSky * max(rippleLight, 0.0) * 0.035;
    float waveFacet = sin(p0) * 0.52 + sin(p1) * 0.31 + sin(p2) * 0.17;
    float crest = smoothstep(0.52, 0.96, waveFacet) * 0.026;
    color += reflectedSky * (waveFacet * 0.045 + crest);
    color *= 0.985 + dot(normal.xz, normalize(vec2(0.74, -0.67))) * 0.28;
    color += u_lightColor * (glint * 0.34 + broadGlint * 0.48 + roughGlint);
    color = mix(color, u_foamColor, foam);
    color += VFXLights_Accumulate(fragPosition, normal, base) * 0.65;
    finalColor = vec4(color, 1.0);
}
