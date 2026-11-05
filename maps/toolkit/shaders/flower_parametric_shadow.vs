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

mat3 FlowerBasisFromAxis(vec3 upAxis, float cosYaw, float sinYaw)
{
    float upLenSq = dot(upAxis, upAxis);
    vec3 up = (upLenSq > 1e-8) ? (upAxis * inversesqrt(upLenSq)) : vec3(0.0, 1.0, 0.0);
    vec3 ref = vec3(cosYaw, 0.0, sinYaw);
    vec3 rawRight = ref - up * dot(ref, up);
    float rLenSq = dot(rawRight, rawRight);
    vec3 right = (rLenSq > 1e-6) ? (rawRight * inversesqrt(rLenSq)) : vec3(-sinYaw, 0.0, cosYaw);
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
    int rawSpecies = int(floor(tex0.w * 0.01 + 0.01));
    float seed = clamp(tex0.w - float(rawSpecies) * 100.0, 0.0, 0.9999);
    int sp = (u_speciesOverride >= 0) ? (u_speciesOverride & 7) : (rawSpecies & 7);

    float stemH = max(tex1.x, 0.16);
    float petalR = max(tex1.y * 1.32, stemH * 0.32);
    float yaw = tex1.z;
    float cosYaw = cos(yaw);
    float sinYaw = sin(yaw);

    int encoded = int(floor(vertexPosition.x + 0.5));
    int part = encoded >> 4;
    int idx = encoded & 15;
    vec2 uv = vertexPosition.yz;

    vec3 rootWorld = rootLocal + u_worldOffset;
    float maxAllowedBend = min(max(u_interactionMaxBend * 0.85, 0.05), stemH * 0.52);
    vec2 windBend = NatureVegetationWindBend(
        rootWorld, vec2(seed, 0.85), u_time, u_windStrength, maxAllowedBend);
    windBend += NatureDominantWindImpact(rootWorld) * 0.85;

    // Đồng bộ biến dạng dẫm cỏ (u_interactionMap) giữa lượt vẽ bóng và lượt vẽ hiển thị
    if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
        vec2 uvOffset = (rootWorld.xz - u_interactionCenter) / u_interactionWorldSize;
        float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
        if (maxAxis < 0.5) {
            vec2 interactionUV = uvOffset + 0.5;
            vec3 sampleValue = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
            vec2 pushDirection = sampleValue.rg * 2.0 - 1.0;
            float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
            edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
            windBend += pushDirection * (sampleValue.b * edgeFeather * maxAllowedBend);
        }
    }

    float rawBendSq = dot(windBend, windBend);
    float bendMag = (rawBendSq > 1e-10) ? (rawBendSq * inversesqrt(rawBendSq)) : 0.0;
    if (bendMag > maxAllowedBend) {
        windBend *= maxAllowedBend / bendMag;
        bendMag = maxAllowedBend;
    }

    float archCurve = (sp == 5) ? 0.34 : 0.14;
    vec3 tipOffset = vec3(
        windBend.x + cosYaw * stemH * archCurve,
        stemH * max(0.76, 1.0 - bendMag * 0.28),
        windBend.y + sinYaw * stemH * archCurve
    );
    vec3 midOffset = vec3(tipOffset.x * 0.38, stemH * 0.50, tipOffset.z * 0.38);

    vec3 localPos = rootLocal;
    if (part == 0) {
        float h = uv.y;
        float om = 1.0 - h;
        vec3 spine = (2.0 * om * h) * midOffset + (h * h) * tipOffset;
        vec3 side = vec3(-sinYaw, 0.0, cosYaw);
        float w = mix(max(0.009, petalR * 0.11), max(0.0045, petalR * 0.055), h);
        localPos = rootLocal + spine + side * ((uv.x - 0.5) * 2.0 * w);
    } else if (part == 1) {
        float leafAngle = yaw + float(idx) * 3.14159265 + (seed - 0.5) * 0.85;
        vec3 lDir = vec3(cos(leafAngle), 0.0, sin(leafAngle));
        vec3 lSide = vec3(-lDir.z, 0.0, lDir.x);
        float t = uv.y;
        float leafLen = stemH * (0.48 + 0.16 * fract(seed * 7.13 + float(idx) * 0.37));
        float leafW = max(0.016, petalR * 0.22) * sin(t * 3.14159265);
        float archY = sin(t * 2.35) * (leafLen * 0.52) - t * t * (leafLen * 0.18);
        vec3 lPos = lDir * (t * leafLen * 0.72) + vec3(0.0, max(0.01, archY), 0.0);
        lPos.xz += windBend * (t * 0.45);
        localPos = rootLocal + lPos + lSide * ((uv.x - 0.5) * 2.0 * leafW);
    } else {
        vec3 headAxis = (sp == 5)
            ? vec3(cosYaw * 0.48, -0.86, sinYaw * 0.48)
            : (tipOffset - midOffset);
        mat3 headBasis = FlowerBasisFromAxis(headAxis, cosYaw, sinYaw);
        vec3 headPos = rootLocal + tipOffset;
        bool isInner = (part == 3);
        float baseAngle = float(idx) * 1.04719755 + (isInner ? 0.5235988 : 0.0) + seed * 6.2831853;
        float cA = cos(baseAngle);
        float sA = sin(baseAngle);
        vec3 radial = headBasis[0] * cA + headBasis[2] * sA;
        vec3 tangent = -headBasis[0] * sA + headBasis[2] * cA;
        float t = uv.y;
        float s = (uv.x - 0.5) * 2.0;
        float lenScale = isInner ? 0.62 : 1.05;
        float widScale = isInner ? 0.26 : 0.42;
        localPos = headPos + radial * (t * petalR * lenScale)
                 + headBasis[1] * (t * petalR * 0.28)
                 + tangent * (s * petalR * widScale * (4.0 * t * (1.0 - t)));
    }

    fragTexCoord = vec2(-1.0, uv.y);
    gl_Position = mvp * vec4(localPos, 1.0);
}