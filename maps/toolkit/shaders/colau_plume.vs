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
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;

uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

uniform sampler2D u_coLauInstances;
uniform int u_lodStride;

out vec3 v_color;
out vec2 v_strandUV;
out float v_distToCam;
out vec3 v_worldPos;
out vec3 v_normal;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

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

    int packedColor = int(floor(tex1.w + 0.5));
    vec3 instanceTint = vec3(
        float((packedColor >> 16) & 255) * (1.0 / 255.0),
        float((packedColor >> 8) & 255) * (1.0 / 255.0),
        float(packedColor & 255) * (1.0 / 255.0)
    );
    if (dot(instanceTint, instanceTint) < 0.05)
        instanceTint = vec3(0.89, 0.90, 0.86);

    float u = vertexTexCoord.x;
    float v = vertexTexCoord.y;
    float rd = vertexTexCoord2.x;
    float rad = vertexTexCoord2.y;
    float isStrand = step(0.5, vertexTangent.w);

    // Scale template model coordinates
    vec3 pModel = vertexPosition * sc;
    pModel.y *= (stemH / 1.65);

    vec3 localPos = RotY(pModel, cYaw, sYaw);
    vec3 T = normalize(RotY(vertexTangent.xyz, cYaw, sYaw));
    vec3 N = normalize(RotY(vertexNormal, cYaw, sYaw));

    vec3 rootWorld = rootLocal + u_worldOffset;
    vec3 worldPos = rootWorld + localPos;

    // Macro wind sway
    float h = clamp(vertexPosition.y / 1.65, 0.0, 1.0);
    float maxAllowedBend = min(max(u_interactionMaxBend * 0.85, 0.05), stemH * 0.42);
    vec2 windBend = NatureVegetationWindBend(
        rootWorld, vec2(seed, 0.85), u_time, u_windStrength, maxAllowedBend);
    windBend += NatureDominantWindImpact(rootWorld) * 0.85;

    // Player / skill interaction push
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

    float bendSq = dot(windBend, windBend);
    float bendMag = (bendSq > 1e-10) ? sqrt(bendSq) : 0.0;
    if (bendMag > maxAllowedBend) {
        windBend *= (maxAllowedBend / bendMag);
        bendMag = maxAllowedBend;
    }

    // Stem bend (quadratic with height)
    vec3 stemOffset = vec3(windBend.x, -0.08 * bendMag, windBend.y) * (h * h);
    worldPos += stemOffset;
    localPos += stemOffset;

    // Micro strand flutter
    if (isStrand > 0.5) {
        float tFlut = u_time * 5.2 + rd * 40.0;
        float sa = sin(tFlut);
        float ca = cos(tFlut * 0.94);
        vec3 flutter = vec3(sa, 0.35 * sa * ca, ca) * 0.016 * pow(u, 1.4) * (0.6 + 0.4 * u_windStrength) * sc;
        worldPos += flutter;
        localPos += flutter;
    }

    // Camera-facing ribbon expansion for strands
    vec3 toCam = u_viewPos - worldPos;
    float distSq = max(dot(toCam, toCam), 1e-6);
    float invDist = inversesqrt(distSq);
    float distToCam = distSq * invDist;
    vec3 V = toCam * invDist;

    if (isStrand > 0.5) {
        vec3 sd = cross(T, V);
        float sdLenSq = dot(sd, sd);
        sd = (sdLenSq > 1e-6) ? (sd * inversesqrt(sdLenSq)) : vec3(0.0, 1.0, 0.0);
        float strandWidth = 0.015 * sc;
        vec3 expand = sd * (v * strandWidth) * (0.65 + 0.75 * rd) * pow(max(0.0, 1.0 - u * 0.94), 0.68);
        worldPos += expand;
        localPos += expand;
    }

    // --- Lighting (Direct Sun, Wrap Diffuse, Backscatter SSS, Kajiya-Kay Specular) ---
    float lLenSq = dot(u_lightDir, u_lightDir);
    vec3 L = (lLenSq > 1e-8) ? (u_lightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 SUN = (dot(u_lightColor, u_lightColor) > 0.05) ? u_lightColor : vec3(1.15, 1.05, 0.88);
    vec3 AMB = (dot(u_ambientColor, u_ambientColor) > 0.05) ? u_ambientColor : vec3(0.32, 0.42, 0.36);

    vec3 ambShade = mix(AMB * 0.65, AMB * 1.15, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    float ndl = dot(N, L);
    float wrap = clamp(ndl * 0.5 + 0.5, 0.0, 1.0);
    wrap *= wrap;

    float alFactor = 1.0;
    vec3 litColor;

    if (isStrand > 0.5) {
        // Backscattering SSS (translucent fiber glow when backlit)
        float forwardDot = clamp(dot(-V, L) * 0.5 + 0.5, 0.0, 1.0);
        float backScatter = forwardDot * forwardDot * forwardDot * forwardDot;

        // Kajiya-Kay fiber specular
        vec3 halfSum = L + V;
        vec3 H = halfSum * inversesqrt(max(dot(halfSum, halfSum), 1e-8));
        float th = dot(T, H);
        float specSin = sqrt(max(1.0 - th * th, 0.0));
        float spec = pow(specSin, 32.0);

        // Albedo: stem core -> plume tint -> light silver/amber tips
        vec3 stemCore = vec3(0.38, 0.32, 0.20);
        vec3 alb = mix(stemCore, instanceTint, smoothstep(0.0, 0.38, u));
        alb = mix(alb, vec3(0.98, 0.98, 0.96), smoothstep(0.45, 1.0, u) * 0.35);
        alb *= (0.88 + 0.24 * rd);

        // Volumetric AO
        float pu = clamp((vertexPosition.y - 0.95) / 0.70, 0.0, 1.0);
        float ao = mix(0.38, 1.0, smoothstep(0.0, 0.85, rad)) * mix(0.55, 1.0, smoothstep(0.0, 0.45, pu));

        litColor = alb * (ambShade * 0.85 + SUN * wrap * 1.05) * ao
            + SUN * alb * backScatter * 1.10 * (0.25 + 0.75 * u) * (0.4 + 0.6 * ao)
            + SUN * spec * 0.35 * (0.3 + 0.7 * u) * ao;
        alFactor = 1.0 - 0.35 * smoothstep(0.72, 1.0, u);
    } else {
        vec3 alb = mix(vec3(0.28, 0.34, 0.16), vec3(0.64, 0.54, 0.32), h);
        litColor = alb * (ambShade * 0.85 + SUN * wrap * 0.92);
        alFactor = 1.0;
    }

    v_color = litColor;
    v_strandUV = vec2(v, alFactor);
    v_distToCam = distToCam;
    v_worldPos = worldPos;
    v_normal = N;

    if (distSq <= 36.5 * 36.5) {
        vec4 world4 = vec4(worldPos, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    gl_Position = mvp * vec4(rootLocal + localPos, 1.0);
}
