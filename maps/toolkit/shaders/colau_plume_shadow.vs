#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec2 vertexTexCoord2;

uniform mat4 mvp;
uniform vec3 u_worldOffset;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_lightDir;

uniform sampler2D u_coLauInstances;
uniform int u_lodStride;

vec3 RotY(vec3 v, float c, float s)
{
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

void main()
{
    int instanceId = gl_InstanceID * max(u_lodStride, 1);
    int texCol = (instanceId & 255) * 2;
    int texRow = instanceId >> 8;

    vec4 tex0 = texelFetch(u_coLauInstances, ivec2(texCol + 0, texRow), 0);
    vec4 tex1 = texelFetch(u_coLauInstances, ivec2(texCol + 1, texRow), 0);

    vec3 rootLocal = tex0.xyz;
    float seed = tex0.w;

    float stemH = max(tex1.x, 1.2);
    float sc = max(tex1.y, 0.4);
    float yaw = tex1.z;
    float cYaw = cos(yaw);
    float sYaw = sin(yaw);

    float u = vertexTexCoord.x;
    float v = vertexTexCoord.y;
    float rd = vertexTexCoord2.x;
    float isStrand = step(0.5, vertexTangent.w);

    vec3 pModel = vertexPosition * sc;
    pModel.y *= (stemH / 1.65);

    vec3 localPos = RotY(pModel, cYaw, sYaw);
    vec3 T = normalize(RotY(vertexTangent.xyz, cYaw, sYaw));

    vec3 rootWorld = rootLocal + u_worldOffset;
    vec3 worldPos = rootWorld + localPos;

    float h = clamp(vertexPosition.y / 1.65, 0.0, 1.0);
    float maxAllowedBend = stemH * 0.42;
    vec2 windBend = NatureVegetationWindBend(
        rootWorld, vec2(seed, 0.85), u_time, u_windStrength, maxAllowedBend);
    windBend += NatureDominantWindImpact(rootWorld) * 0.85;

    float bendSq = dot(windBend, windBend);
    float bendMag = (bendSq > 1e-10) ? sqrt(bendSq) : 0.0;
    if (bendMag > maxAllowedBend) {
        windBend *= (maxAllowedBend / bendMag);
        bendMag = maxAllowedBend;
    }

    vec3 stemOffset = vec3(windBend.x, -0.08 * bendMag, windBend.y) * (h * h);
    worldPos += stemOffset;
    localPos += stemOffset;

    // Face the light source for shadow casting
    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 L = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    if (isStrand > 0.5) {
        vec3 sd = cross(T, L);
        float sdLenSq = dot(sd, sd);
        sd = (sdLenSq > 1e-6) ? (sd * inversesqrt(sdLenSq)) : vec3(0.0, 1.0, 0.0);
        float strandWidth = 0.016 * sc;
        vec3 expand = sd * (v * strandWidth) * (0.65 + 0.75 * rd) * pow(max(0.0, 1.0 - u * 0.94), 0.68);
        localPos += expand;
    }

    gl_Position = mvp * vec4(rootLocal + localPos, 1.0);
}
