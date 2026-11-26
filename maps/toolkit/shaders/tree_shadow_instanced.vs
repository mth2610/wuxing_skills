#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform float u_time;
uniform float u_windStrength;
uniform sampler2D u_treeInstanceTex;
uniform int u_instanceBaseOffset;
uniform float u_treeGlobalDryness;
uniform float u_treeGlobalScorch;

out vec2 fragTexCoord;

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

    vec2 cleanUV = vertexTexCoord;
    if (vertexTexCoord.x >= 0.0) {
        float rankBucket = floor(vertexTexCoord.x);
        float shellBucket = floor(vertexTexCoord.y);
        float leafRank = (rankBucket + 0.5) * (1.0 / 64.0);
        cleanUV = vertexTexCoord - vec2(rankBucket, shellBucket);

        // Mirror visible-pass defoliation so dry trees cast crisp bare-branch shadows!
        float retention = (1.0 - smoothstep(0.18, 0.94, dryness)) * (1.0 - smoothstep(0.35, 0.68, scorch));
        if (leafRank > retention) {
            fragTexCoord = vec2(0.0);
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

    float compliance = vertexColor.a;
    if (vertexTexCoord.x >= 0.0 && dryness > 0.05) {
        float tipMask = clamp((compliance - 0.30) * 1.45, 0.0, 1.0);
        world.y -= dryness * 0.14 * tipMask * scaleY;
    }

    float hNorm = clamp(lp.y / max(scaleY * 5.2, 1.0), 0.0, 1.0);
    float stiffScale = 1.0 - 0.50 * dryness;
    if (compliance > 0.001) {
        vec2 macroBend = NatureVegetationWindBend(instPos, vec2(phase, hNorm), u_time, u_windStrength, 0.90);
        vec2 impactBend = NatureDominantWindImpact(world);
        vec2 totalBend = (macroBend + impactBend) * stiffScale;
        if (dot(totalBend, totalBend) > 1e-8) {
            float trunkSway = hNorm * hNorm * 0.40;
            world.xz += totalBend * trunkSway;
            world.y -= dot(totalBend, totalBend) * 0.06 * hNorm;
        }
    }

    fragTexCoord = cleanUV;
    gl_Position = mvp * vec4(world, 1.0);
}
