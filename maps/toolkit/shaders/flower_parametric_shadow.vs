#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform vec3 u_worldOffset;
uniform float u_time;
uniform float u_windStrength;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

uniform sampler2D u_flowerInstances;
uniform int u_speciesOverride;
uniform int u_lodStride;

out vec2 fragTexCoord;

mat3 FlowerBasisFromAxis(vec3 upAxis, float yaw)
{
    vec3 up = normalize(upAxis);
    vec3 ref = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 right = normalize(ref - up * dot(ref, up));
    vec3 fwd = cross(up, right);
    return mat3(right, up, fwd);
}

void main()
{
    int instanceId = gl_InstanceID * max(u_lodStride, 1);
    int col = (instanceId & 255) * 2;
    int row = instanceId >> 8;

    vec4 tex0 = texelFetch(u_flowerInstances, ivec2(col + 0, row), 0);
    vec4 tex1 = texelFetch(u_flowerInstances, ivec2(col + 1, row), 0);

    vec3 rootLocal = tex0.xyz;
    int rawSpecies = int(floor(tex0.w / 100.0 + 0.01));
    float seed = clamp(tex0.w - float(rawSpecies) * 100.0, 0.0, 0.9999);
    int sp = (u_speciesOverride >= 0) ? (u_speciesOverride & 7) : (rawSpecies & 7);

    float stemH = max(tex1.x, 0.16);
    float petalR = max(tex1.y * 1.32, stemH * 0.32);
    float yaw = tex1.z;

    int encoded = int(floor(vertexPosition.x + 0.5));
    int part = encoded >> 4;
    int idx = encoded & 15;
    vec2 uv = vertexPosition.yz;

    vec3 rootWorld = rootLocal + u_worldOffset;
    float maxAllowedBend = min(max(u_interactionMaxBend * 0.85, 0.05), stemH * 0.52);
    vec2 windBend = NatureVegetationWindBend(
        rootWorld, vec2(seed, 0.85), u_time, u_windStrength, maxAllowedBend);
    windBend += NatureDominantWindImpact(rootWorld) * 0.85;

    float rawBendSq = dot(windBend, windBend);
    if (rawBendSq > maxAllowedBend * maxAllowedBend) {
        windBend *= maxAllowedBend * inversesqrt(rawBendSq);
    }

    float archCurve = (sp == 5) ? 0.34 : 0.14;
    vec3 tipOffset = vec3(
        windBend.x + cos(yaw) * stemH * archCurve,
        stemH * 0.90,
        windBend.y + sin(yaw) * stemH * archCurve
    );

    vec3 localPos = rootLocal;
    if (part == 0) {
        float h = uv.y;
        vec3 side = vec3(-sin(yaw), 0.0, cos(yaw));
        float w = mix(max(0.009, petalR * 0.11), max(0.005, petalR * 0.06), h);
        localPos = rootLocal + tipOffset * h + side * ((uv.x - 0.5) * 2.0 * w);
    } else {
        mat3 headBasis = FlowerBasisFromAxis(tipOffset, yaw);
        vec3 headPos = rootLocal + tipOffset;
        float baseAngle = (float(idx) / 6.0) * 6.2831853 + seed * 6.2831853;
        vec3 radial = headBasis[0] * cos(baseAngle) + headBasis[2] * sin(baseAngle);
        vec3 tangent = -headBasis[0] * sin(baseAngle) + headBasis[2] * cos(baseAngle);
        float t = uv.y;
        float s = (uv.x - 0.5) * 2.0;
        localPos = headPos + radial * (t * petalR) + headBasis[1] * (t * petalR * 0.25) + tangent * (s * petalR * 0.42 * sin(t * 3.14159265));
    }

    fragTexCoord = vec2(0.5, 0.5);
    gl_Position = mvp * vec4(localPos, 1.0);
}
