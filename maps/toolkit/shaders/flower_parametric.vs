#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform vec3 u_worldOffset;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

uniform sampler2D u_flowerInstances;
uniform int u_speciesOverride;
uniform int u_lodStride;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 v_uv;
flat out float v_part;
flat out float v_species;
flat out float v_seed;
out float v_ao;
flat out vec3 v_customTint;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

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

void EvaluateFlowerGeometry(
    vec3 rootPos, float stemH, float petalR, float yaw, float cosYaw, float sinYaw,
    float seed, int sp, int part, int idx, vec2 uv, vec2 windBend, float bendMag, float flutter,
    out vec3 outLocalPos, out vec3 outNormal, out float outAO)
{
    float archCurve = (sp == 5) ? 0.34 : 0.14;
    vec3 tipOffset = vec3(
        windBend.x + cosYaw * stemH * archCurve,
        stemH * max(0.76, 1.0 - bendMag * 0.28),
        windBend.y + sinYaw * stemH * archCurve
    );
    vec3 midOffset = vec3(
        tipOffset.x * 0.38,
        stemH * 0.50,
        tipOffset.z * 0.38
    );

    if (part == 0) {
        float h = uv.y;
        float om = 1.0 - h;
        vec3 spine = (2.0 * om * h) * midOffset + (h * h) * tipOffset;
        vec3 rawTang = 2.0 * om * midOffset + 2.0 * h * (tipOffset - midOffset);
        vec3 tang = rawTang * inversesqrt(max(dot(rawTang, rawTang), 1e-8));
        vec3 side = vec3(-sinYaw, 0.0, cosYaw);
        float w = mix(max(0.009, petalR * 0.11), max(0.0045, petalR * 0.055), h);
        outLocalPos = rootPos + spine + side * ((uv.x - 0.5) * 2.0 * w);
        vec3 rawN = cross(side, tang) + side * ((uv.x - 0.5) * 0.65);
        outNormal = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));
        outAO = mix(0.28, 0.92, h);
        return;
    }

    if (part == 1) {
        float leafAngle = yaw + float(idx) * 3.14159265 + (seed - 0.5) * 0.85;
        vec3 lDir = vec3(cos(leafAngle), 0.0, sin(leafAngle));
        vec3 lSide = vec3(-lDir.z, 0.0, lDir.x);
        float t = uv.y;
        float leafLen = stemH * (0.48 + 0.16 * fract(seed * 7.13 + float(idx) * 0.37));
        float leafW = max(0.016, petalR * 0.22) * sin(t * 3.14159265);
        float archY = sin(t * 2.35) * (leafLen * 0.52) - t * t * (leafLen * 0.18);
        vec3 lPos = lDir * (t * leafLen * 0.72) + vec3(0.0, max(0.01, archY), 0.0);
        lPos.xz += windBend * (t * 0.45);
        outLocalPos = rootPos + lPos + lSide * ((uv.x - 0.5) * 2.0 * leafW);
        vec3 rawN = vec3(-lDir.x * 0.25, 0.88, -lDir.z * 0.25) + lSide * ((uv.x - 0.5) * 0.55);
        outNormal = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));
        outAO = mix(0.32, 0.95, t);
        return;
    }

    vec3 headAxis = (sp == 5)
        ? vec3(cosYaw * 0.48, -0.86, sinYaw * 0.48)
        : (tipOffset - midOffset);
    mat3 headBasis = FlowerBasisFromAxis(headAxis, cosYaw, sinYaw);
    vec3 headPos = rootPos + tipOffset;

    bool isInner = (part == 3);
    float baseAngle = float(idx) * 1.04719755 + (isInner ? 0.5235988 : 0.0) + seed * 6.2831853;
    float cA = cos(baseAngle);
    float sA = sin(baseAngle);
    vec3 radial = headBasis[0] * cA + headBasis[2] * sA;
    vec3 tangent = -headBasis[0] * sA + headBasis[2] * cA;

    float t = clamp(uv.y, 0.0, 1.0);
    float t2 = t * t;
    float sqrtT = sqrt(t);
    float t075 = sqrt(t * sqrtT); // t^0.75 exact without pow()
    float sinPiT = sin(t * 3.14159265);
    float sinShaped = sin(t075 * 3.14159265);
    float s = (uv.x - 0.5) * 2.0;

    float len = petalR;
    float wid = petalR * 0.46;
    float cupY = 0.0;
    float radialDist = t;

    if (sp == 2) {
        if (!isInner) {
            len = petalR * 1.18;
            wid = petalR * 0.22 * sinPiT;
            cupY = sinPiT * (petalR * 0.42) - (t2 * sqrt(sqrtT)) * (petalR * 0.52);
            radialDist = t * len + sin(t * 6.2831853) * (petalR * 0.06);
        } else {
            len = petalR * 1.42;
            wid = petalR * 0.065 * (1.0 - t * 0.72);
            cupY = -t * (petalR * 0.16) + (t2 * sqrtT) * (petalR * 0.68);
            radialDist = t * len;
        }
    } else if (sp == 3) {
        len = petalR * (isInner ? 0.58 : 1.14);
        wid = petalR * (isInner ? 0.28 : 0.42) * sinShaped;
        cupY = (isInner ? 0.28 : 0.16) * t * petalR + sinPiT * (petalR * 0.18) - (t2 * sqrtT) * (petalR * 0.24);
        radialDist = t * len;
    } else if (sp == 0 || sp == 7) {
        len = petalR * (isInner ? 0.68 : 1.02);
        wid = petalR * (isInner ? 0.38 : 0.50) * sinShaped;
        cupY = (t * sqrt(sqrtT)) * petalR * (isInner ? 0.68 : 0.44);
        radialDist = sin(t * 1.38) * len * (isInner ? 0.72 : 0.94);
    } else if (sp == 1) {
        len = petalR * (isInner ? 0.52 : 1.12);
        wid = petalR * (isInner ? 0.22 : 0.28) * sinPiT;
        cupY = (t * sqrtT) * petalR * (isInner ? 0.36 : 0.46) + t * petalR * 0.08;
        radialDist = t * len;
    } else if (sp == 5 || sp == 6) {
        len = petalR * (isInner ? 0.55 : 0.96);
        wid = petalR * (isInner ? 0.14 : 0.44) * sinShaped;
        cupY = t * len * 0.92;
        radialDist = sin(t * 1.65) * petalR * (isInner ? 0.18 : 0.54) + (t2 * t) * petalR * 0.22;
    } else {
        len = petalR * (isInner ? 0.56 : 1.05);
        wid = petalR * (isInner ? 0.26 : 0.48) * sinShaped;
        cupY = sin(t * 2.1) * petalR * (isInner ? 0.34 : 0.26);
        radialDist = t * len;
    }

    float cupDish = (1.0 - s * s) * (-0.08 * petalR * t);
    cupY += cupDish + flutter * t * petalR * 0.14;

    vec3 localPetal = radial * radialDist + headBasis[1] * cupY + tangent * (s * wid);
    outLocalPos = headPos + localPetal;

    vec3 rawPNorm = (sp == 5 || sp == 6)
        ? (radial * 0.85 - headBasis[1] * 0.32 + tangent * (s * 0.35))
        : (headBasis[1] * 0.82 - radial * (0.36 * t) + tangent * (s * 0.42));
    outNormal = rawPNorm * inversesqrt(max(dot(rawPNorm, rawPNorm), 1e-8));
    outAO = mix(0.46, 1.0, clamp(t * 1.15, 0.0, 1.0));
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

    int packedColor = int(floor(tex1.w + 0.5));
    v_customTint = vec3(
        float((packedColor >> 16) & 255) * (1.0 / 255.0),
        float((packedColor >> 8) & 255) * (1.0 / 255.0),
        float(packedColor & 255) * (1.0 / 255.0)
    );

    int encoded = int(floor(vertexPosition.x + 0.5));
    int part = encoded >> 4;
    int idx = encoded & 15;
    vec2 uv = vertexPosition.yz;

    vec3 rootWorld = rootLocal + u_worldOffset;
    float maxAllowedBend = min(max(u_interactionMaxBend * 0.85, 0.05), stemH * 0.52);
    vec2 windBend = NatureVegetationWindBend(
        rootWorld, vec2(seed, 0.85), u_time, u_windStrength, maxAllowedBend);
    windBend += NatureDominantWindImpact(rootWorld) * 0.85;

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

    float flutter = sin(mod(u_time * 3.2, 6283.185307) + seed * 6.2831853 + float(idx) * 1.047) *
                    (0.04 + 0.08 * min(u_windStrength, 1.5));

    vec3 localPos, localNormal;
    float ao;
    EvaluateFlowerGeometry(
        rootLocal, stemH, petalR, yaw, cosYaw, sinYaw,
        seed, sp, part, idx, uv, windBend, bendMag, flutter,
        localPos, localNormal, ao);

    vec3 worldPos = localPos + u_worldOffset;
    fragPosition = worldPos;
    fragNormal = localNormal;
    v_uv = uv;
    v_part = float(part);
    v_species = float(sp);
    v_seed = seed;
    v_ao = ao;

    vec3 toCam = worldPos - u_viewPos;
    if (dot(toCam, toCam) <= 36.5 * 36.5) {
        vec4 world4 = vec4(worldPos, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    gl_Position = mvp * vec4(localPos, 1.0);
}