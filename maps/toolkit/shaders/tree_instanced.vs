#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord; // .x < 0.0 => bark (y=AO); .x >= 0.0 => foliage atlas UV (0..1)
in vec3 vertexNormal;
in vec4 vertexColor; // rgb = vertex tint * baked AO, a = wind compliance (0=base, 1=outer foliage)

uniform mat4 mvp;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_treeInstanceTex;
uniform float u_treeDrawDistSq;
uniform int u_instanceBaseOffset;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor; // rgb = vertex color * instance tint, a = canopy mask (0=bark, 1=foliage)
out vec2 fragTexCoord;
out float v_localY;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

vec4 FetchTreeTexel(int texelIdx)
{
    int tx = texelIdx & 511;
    int ty = texelIdx >> 9;
    return texelFetch(u_treeInstanceTex, ivec2(tx, ty), 0);
}

void main()
{
    int base = (gl_InstanceID + u_instanceBaseOffset) * 2;
    vec4 t0 = FetchTreeTexel(base);
    vec4 t1 = FetchTreeTexel(base + 1);

    vec3 instPos = t0.xyz;
    float rotRad = t0.w;
    float scaleXZ = t1.x;
    float scaleY = t1.y;
    float phase = t1.z;
    int packedRGB = int(t1.w + 0.5);

    vec3 toCam = instPos - u_viewPos;
    float distSq = dot(toCam, toCam);
    if (distSq > u_treeDrawDistSq) {
        fragPosition = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragColor = vec4(0.0);
        fragTexCoord = vec2(0.0);
        v_localY = 0.0;
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    float c = cos(rotRad);
    float s = sin(rotRad);
    vec3 lp = vec3(vertexPosition.x * scaleXZ, vertexPosition.y * scaleY, vertexPosition.z * scaleXZ);
    vec3 world = vec3(
        instPos.x + lp.x * c + lp.z * s,
        instPos.y + lp.y,
        instPos.z - lp.x * s + lp.z * c
    );
    vec3 wn = vec3(
        vertexNormal.x * c + vertexNormal.z * s,
        vertexNormal.y,
        -vertexNormal.x * s + vertexNormal.z * c
    );

    float isFoliage = (vertexTexCoord.x >= 0.0) ? 1.0 : 0.0;

    // 3-stage phase-coupled wind sway driven strictly by live Wind_GetMacro + local impacts
    float hNorm = clamp(lp.y / max(scaleY * 5.2, 1.0), 0.0, 1.0);
    float compliance = vertexColor.a;
    if (compliance > 0.001) {
        vec2 macroBend = NatureVegetationWindBend(instPos, vec2(phase, hNorm), u_time, u_windStrength, 0.90);
        vec2 impactBend = NatureDominantWindImpact(world);
        vec2 totalBend = macroBend + impactBend;
        float bendSpeed = length(totalBend);
        if (bendSpeed > 1e-4) {
            float trunkSway = hNorm * hNorm * 0.42;
            float branchPhase = u_time * 2.1 + phase * 6.283 + lp.x * 0.85 + lp.z * 0.92;
            float frondPhase = u_time * 4.6 + phase * 12.566 + lp.x * 2.4 + lp.z * 2.7;
            float gustMag = min(bendSpeed * 0.35, 0.25);
            vec2 secondarySway = vec2(sin(branchPhase), cos(branchPhase * 0.87)) * (gustMag * 0.36 * compliance);
            vec2 frondFlutter = vec2(sin(frondPhase), cos(frondPhase * 1.13)) * (gustMag * 0.22 * compliance * isFoliage);
            world.xz += totalBend * trunkSway + secondarySway + frondFlutter;
            world.y -= dot(totalBend, totalBend) * 0.06 * hNorm;
        }
    }

    int r = (packedRGB >> 16) & 255;
    int g = (packedRGB >> 8) & 255;
    int b = packedRGB & 255;
    vec3 tint = vec3(float(r), float(g), float(b)) * (1.0 / 255.0);

    fragPosition = world;
    fragNormal = wn * inversesqrt(max(dot(wn, wn), 1e-8));
    fragColor = vec4(mix(vertexColor.rgb, vertexColor.rgb * tint, isFoliage), isFoliage);
    fragTexCoord = vertexTexCoord;
    v_localY = lp.y;

    if (distSq <= 68.0 * 68.0) {
        vec4 world4 = vec4(world, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    gl_Position = mvp * vec4(world, 1.0);
}
