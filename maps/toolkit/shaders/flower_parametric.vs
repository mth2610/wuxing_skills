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
out float v_part;
out float v_species;
out float v_seed;
out float v_ao;
out vec3 v_customTint;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

mat3 FlowerBasisFromAxis(vec3 upAxis, float yaw)
{
    vec3 up = normalize(upAxis);
    vec3 ref = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 right = normalize(ref - up * dot(ref, up));
    vec3 fwd = cross(up, right);
    return mat3(right, up, fwd);
}

void EvaluateFlowerGeometry(
    vec3 rootPos, float stemH, float petalR, float yaw, float seed, int sp,
    int part, int idx, vec2 uv, vec2 windBend, float flutter,
    out vec3 outLocalPos, out vec3 outNormal, out float outAO)
{
    float bendMag = length(windBend);
    vec2 bendDir = (bendMag > 1e-4) ? (windBend / bendMag) : vec2(cos(yaw), sin(yaw));

    float archCurve = (sp == 5) ? 0.34 : 0.14;
    vec3 tipOffset = vec3(
        windBend.x + cos(yaw) * stemH * archCurve,
        stemH * max(0.76, 1.0 - bendMag * 0.28),
        windBend.y + sin(yaw) * stemH * archCurve
    );
    vec3 midOffset = vec3(
        tipOffset.x * 0.38,
        stemH * 0.50,
        tipOffset.z * 0.38
    );

    if (part == 0) {
        float h = uv.y;
        float om = 1.0 - h;
        vec3 spine = om * om * vec3(0.0) + 2.0 * om * h * midOffset + h * h * tipOffset;
        vec3 tang = normalize(2.0 * om * midOffset + 2.0 * h * (tipOffset - midOffset));
        vec3 side = normalize(vec3(- sin(yaw), 0.0, cos(yaw)));
        float w = mix(max(0.009, petalR * 0.11), max(0.0045, petalR * 0.055), h);
        outLocalPos = rootPos + spine + side * ((uv.x - 0.5) * 2.0 * w);
        outNormal = normalize(cross(side, tang) + side * (uv.x - 0.5) * 0.65);
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
        outNormal = normalize(vec3(-lDir.x * 0.25, 0.88, -lDir.z * 0.25) + lSide * (uv.x - 0.5) * 0.55);
        outAO = mix(0.32, 0.95, t);
        return;
    }

    vec3 headAxis = normalize(tipOffset - midOffset);
    if (sp == 5) {
        headAxis = normalize(vec3(cos(yaw) * 0.48, -0.86, sin(yaw) * 0.48));
    }
    mat3 headBasis = FlowerBasisFromAxis(headAxis, yaw);
    vec3 headPos = rootPos + tipOffset;

    bool isInner = (part == 3);
    float pCount = (sp == 2 && !isInner) ? 6.0 : ((sp == 1 && !isInner) ? 6.0 : 6.0);
    float baseAngle = (float(idx) / pCount) * 6.2831853 + (isInner ? 0.5235988 : 0.0) + seed * 6.2831853;
    vec3 radial = headBasis[0] * cos(baseAngle) + headBasis[2] * sin(baseAngle);
    vec3 tangent = -headBasis[0] * sin(baseAngle) + headBasis[2] * cos(baseAngle);

    float t = uv.y;
    float s = (uv.x - 0.5) * 2.0;

    float len = petalR;
    float wid = petalR * 0.46;
    float cupY = 0.0;
    float radialDist = t;

    if (sp == 2) {
        if (!isInner) {
            len = petalR * 1.18;
            wid = petalR * 0.22 * sin(t * 3.14159265);
            cupY = sin(t * 3.14159265) * (petalR * 0.42) - pow(t, 2.2) * (petalR * 0.52);
            radialDist = t * len + sin(t * 6.2831853) * (petalR * 0.06);
        } else {
            len = petalR * 1.42;
            wid = petalR * 0.065 * (1.0 - t * 0.72);
            cupY = -t * (petalR * 0.16) + pow(t, 2.4) * (petalR * 0.68);
            radialDist = t * len;
        }
    } else if (sp == 3) {
        len = petalR * (isInner ? 0.58 : 1.14);
        wid = petalR * (isInner ? 0.28 : 0.42) * sin(pow(t, 0.75) * 3.14159265);
        cupY = (isInner ? 0.28 : 0.16) * t * petalR + sin(t * 3.14159265) * (petalR * 0.18) - pow(t, 2.6) * (petalR * 0.24);
        radialDist = t * len;
    } else if (sp == 0 || sp == 7) {
        len = petalR * (isInner ? 0.68 : 1.02);
        wid = petalR * (isInner ? 0.38 : 0.50) * sin(pow(t, 0.78) * 3.14159265);
        cupY = pow(t, 1.35) * petalR * (isInner ? 0.68 : 0.44);
        radialDist = sin(t * 1.38) * len * (isInner ? 0.72 : 0.94);
    } else if (sp == 1) {
        len = petalR * (isInner ? 0.52 : 1.12);
        wid = petalR * (isInner ? 0.22 : 0.28) * sin(t * 3.14159265);
        cupY = pow(t, 1.6) * petalR * (isInner ? 0.36 : 0.46) + t * petalR * 0.08;
        radialDist = t * len;
    } else if (sp == 5 || sp == 6) {
        len = petalR * (isInner ? 0.55 : 0.96);
        wid = petalR * (isInner ? 0.14 : 0.44) * sin(pow(t, 0.65) * 3.14159265);
        cupY = t * len * 0.92;
        radialDist = sin(t * 1.65) * petalR * (isInner ? 0.18 : 0.54) + pow(t, 3.0) * petalR * 0.22;
    } else {
        len = petalR * (isInner ? 0.56 : 1.05);
        wid = petalR * (isInner ? 0.26 : 0.48) * sin(pow(t, 0.72) * 3.14159265);
        cupY = sin(t * 2.1) * petalR * (isInner ? 0.34 : 0.26);
        radialDist = t * len;
    }

    float cupDish = (1.0 - s * s) * (-0.08 * petalR * t);
    cupY += cupDish + flutter * t * petalR * 0.14;

    vec3 localPetal = radial * radialDist + headBasis[1] * cupY + tangent * (s * wid);
    outLocalPos = headPos + localPetal;

    vec3 pNorm = normalize(headBasis[1] * 0.82 - radial * (0.36 * t) + tangent * (s * 0.42));
    if (sp == 5 || sp == 6) {
        pNorm = normalize(radial * 0.85 - headBasis[1] * 0.32 + tangent * (s * 0.35));
    }
    outNormal = pNorm;
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
    int rawSpecies = int(floor(tex0.w / 100.0 + 0.01));
    float seed = clamp(tex0.w - float(rawSpecies) * 100.0, 0.0, 0.9999);
    int sp = (u_speciesOverride >= 0) ? (u_speciesOverride & 7) : (rawSpecies & 7);

    float stemH = max(tex1.x, 0.16);
    float petalR = max(tex1.y * 1.32, stemH * 0.32);
    float yaw = tex1.z;

    int packedColor = int(floor(tex1.w + 0.5));
    v_customTint = vec3(
        float((packedColor >> 16) & 255) / 255.0,
        float((packedColor >> 8) & 255) / 255.0,
        float(packedColor & 255) / 255.0
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
    if (rawBendSq > maxAllowedBend * maxAllowedBend) {
        windBend *= maxAllowedBend * inversesqrt(rawBendSq);
    }

    float flutter = sin(u_time * 3.2 + seed * 6.2831853 + float(idx) * 1.047) * (0.04 + 0.08 * min(u_windStrength, 1.5));

    vec3 localPos, localNormal;
    float ao;
    EvaluateFlowerGeometry(
        rootLocal, stemH, petalR, yaw, seed, sp,
        part, idx, uv, windBend, flutter,
        localPos, localNormal, ao);

    vec3 worldPos = localPos + u_worldOffset;
    fragPosition = worldPos;
    fragNormal = localNormal;
    v_uv = uv;
    v_part = float(part);
    v_species = float(sp);
    v_seed = seed;
    v_ao = ao;
    v_lightSpace = u_lightVP * vec4(worldPos, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(worldPos, 1.0);

    gl_Position = mvp * vec4(localPos, 1.0);
}
