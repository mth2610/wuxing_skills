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
    float phase = t1.z;

    float c = cos(rotRad);
    float s = sin(rotRad);
    vec3 lp = vec3(vertexPosition.x * scaleXZ, vertexPosition.y * scaleY, vertexPosition.z * scaleXZ);
    vec3 world = vec3(
        instPos.x + lp.x * c + lp.z * s,
        instPos.y + lp.y,
        instPos.z - lp.x * s + lp.z * c
    );

    float hNorm = clamp(lp.y / max(scaleY * 4.8, 1.0), 0.0, 1.0);
    float compliance = vertexColor.a;
    if (compliance > 0.001) {
        vec2 macroBend = NatureVegetationWindBend(instPos, vec2(phase, hNorm), u_time, u_windStrength, 0.90);
        vec2 impactBend = NatureDominantWindImpact(world);
        vec2 totalBend = macroBend + impactBend;
        if (dot(totalBend, totalBend) > 1e-8) {
            float trunkSway = hNorm * hNorm * 0.42;
            world.xz += totalBend * trunkSway;
            world.y -= dot(totalBend, totalBend) * 0.06 * hNorm;
        }
    }

    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(world, 1.0);
}
