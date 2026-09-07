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
    // Depth absorption: deep center turquoise to clear shallow shore
    float depth = smoothstep(0.25, 0.92, radial);
    vec3 base = mix(u_deepColor, u_shallowColor, depth);
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.5);
    vec3 halfDir = normalize(u_lightDir + viewDir);
    float glint = pow(max(dot(normal, halfDir), 0.0), 96.0);
    float broadGlint = pow(max(dot(normal, halfDir), 0.0), 16.0) * 0.18;
    float roughGlint = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.14;

    float shallows = smoothstep(0.68, 0.98, radial);
    float shoreline = smoothstep(0.92, 0.995, radial);
    float broken = sin(fragWorldXZ.x * 4.2 + t * 0.48)
                 + sin(fragWorldXZ.y * 4.8 - t * 0.42);
    float foam = shoreline * smoothstep(0.85, 1.65, broken) * 0.28;

    vec3 reflectionDir = reflect(-viewDir, normal);
    float skyFacing = smoothstep(-0.15, 0.85, reflectionDir.y);
    vec3 reflectedSky = mix(u_ambientColor * vec3(0.65, 0.75, 0.95), u_lightColor * 0.72, skyFacing);

    vec3 color = base * (0.42 + u_ambientColor * 0.75 + u_lightColor * 0.15);
    color += reflectedSky * (0.22 + fresnel * 0.58);
    color = mix(color, u_shallowColor * 0.92, shallows * 0.42);

    // Caustics & ripple reflections
    float rippleLight = sin(fragWorldXZ.x * 1.52 + t * 0.65)
                      * sin(fragWorldXZ.y * 1.88 - t * 0.55);
    color += reflectedSky * max(rippleLight, 0.0) * 0.045;
    float waveFacet = sin(p0) * 0.52 + sin(p1) * 0.31 + sin(p2) * 0.17;
    float crest = smoothstep(0.48, 0.95, waveFacet) * 0.035;
    color += reflectedSky * (waveFacet * 0.048 + crest);

    // Soft bank transition: blend water edge toward wet silt tone to erase dark seam
    vec3 bankSiltTone = vec3(0.28, 0.32, 0.26);
    color = mix(color, bankSiltTone, shoreline * 0.65);

    color *= 0.985 + dot(normal.xz, normalize(vec2(0.74, -0.67))) * 0.25;
    color += u_lightColor * (glint * 0.42 + broadGlint * 0.52 + roughGlint);
    color = mix(color, u_foamColor, foam);
    color += VFXLights_Accumulate(fragPosition, normal, base) * 0.65;
    finalColor = vec4(color, 1.0);
}
