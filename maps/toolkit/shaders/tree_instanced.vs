#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord; // .x < 0.0 => bark (x = -1 - 0.25*order, y = root-to-tip t); .x >= 0.0 => packed (rankBucket + u, shellBucket + v)
in vec3 vertexNormal;
in vec4 vertexColor;    // rgb = vertex botanical albedo, a = wind compliance (0=root, 1=outer tip)

uniform mat4 mvp;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_treeInstanceTex;
uniform float u_treeDrawDistSq;
uniform int u_instanceBaseOffset;
uniform float u_treeGlobalDryness;
uniform float u_treeGlobalScorch;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;    // rgb = vertex color * instance tint, a = canopy mask (0=bark, 1=foliage)
out vec2 fragTexCoord; // Clean [0..1] atlas UV for foliage; bark sentinel & continuous t for bark
out float v_localY;
out vec4 v_treeState;  // x = dryness, y = scorch, z = canopyShell [0..1], w = leafRank [0..1]
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

    int packedState = int(floor(t1.z + 0.5));
    float phase = float(packedState & 255) * (1.0 / 255.0);
    float instDry = float((packedState >> 8) & 255) * (1.0 / 255.0);
    float instSco = float((packedState >> 16) & 15) * (1.0 / 15.0);
    float dryness = clamp(max(instDry, u_treeGlobalDryness), 0.0, 1.0);
    float scorch = clamp(max(instSco, u_treeGlobalScorch), 0.0, 1.0);

    int packedRGB = int(floor(t1.w + 0.5));

    vec3 toCam = instPos - u_viewPos;
    float distSq = dot(toCam, toCam);
    if (distSq > u_treeDrawDistSq) {
        fragPosition = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragColor = vec4(0.0);
        fragTexCoord = vec2(0.0);
        v_localY = 0.0;
        v_treeState = vec4(0.0);
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    float isFoliage = (vertexTexCoord.x >= 0.0) ? 1.0 : 0.0;
    float leafRank = 0.5;
    float canopyShell = 0.5;
    vec2 cleanUV = vertexTexCoord;

    if (isFoliage > 0.5) {
        float rankBucket = floor(vertexTexCoord.x);
        float shellBucket = floor(vertexTexCoord.y);
        leafRank = (rankBucket + 0.5) * (1.0 / 64.0);
        canopyShell = (shellBucket + 0.5) * (1.0 / 64.0);
        cleanUV = vertexTexCoord - vec2(rankBucket, shellBucket);

        // Zero-overdraw progressive vertex-shader leaf shedding as tree dries or scorches
        float retention = (1.0 - smoothstep(0.18, 0.94, dryness)) * (1.0 - smoothstep(0.35, 0.68, scorch));
        if (leafRank > retention) {
            fragPosition = vec3(0.0);
            fragNormal = vec3(0.0, 1.0, 0.0);
            fragColor = vec4(0.0);
            fragTexCoord = vec2(0.0);
            v_localY = 0.0;
            v_treeState = vec4(dryness, scorch, canopyShell, leafRank);
            v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
            v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
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

    // Subtle turgor-loss droop on surviving dry foliage tips
    float compliance = vertexColor.a;
    if (isFoliage > 0.5 && dryness > 0.05) {
        float tipMask = clamp((compliance - 0.30) * 1.45, 0.0, 1.0);
        world.y -= dryness * 0.14 * tipMask * scaleY;
    }

    // 3-stage phase-coupled wind sway driven strictly by live Wind_GetMacro + local impacts
    float hNorm = clamp(lp.y / max(scaleY * 5.2, 1.0), 0.0, 1.0);
    float stiffScale = 1.0 - 0.50 * dryness;
    if (compliance > 0.001) {
        vec2 macroBend = NatureVegetationWindBend(instPos, vec2(phase, hNorm), u_time, u_windStrength, 0.90);
        vec2 impactBend = NatureDominantWindImpact(world);
        vec2 totalBend = (macroBend + impactBend) * stiffScale;
        float bendSpeed = length(totalBend);
        if (bendSpeed > 1e-4) {
            float trunkSway = hNorm * hNorm * 0.40;
            float branchPhase = u_time * 2.1 + phase * 6.283 + lp.x * 0.85 + lp.z * 0.92;
            float frondPhase = u_time * 4.6 + phase * 12.566 + lp.x * 2.4 + lp.z * 2.7;
            float gustMag = min(bendSpeed * 0.35, 0.24);
            // Petiole-pinned flutter: 0 at twig attachment, ramps smoothly toward outer leaf tip
            float tipFlutterMask = clamp((compliance - 0.38) * 1.62, 0.0, 1.0) * isFoliage;
            vec2 secondarySway = vec2(sin(branchPhase), cos(branchPhase * 0.87)) * (gustMag * 0.32 * compliance);
            vec2 frondFlutter = vec2(sin(frondPhase), cos(frondPhase * 1.13)) * (gustMag * 0.20 * tipFlutterMask);
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
    fragTexCoord = cleanUV;
    v_localY = lp.y;
    v_treeState = vec4(dryness, scorch, canopyShell, leafRank);

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
