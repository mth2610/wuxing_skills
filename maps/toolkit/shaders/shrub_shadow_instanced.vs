#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;
uniform float u_time;
uniform float u_windStrength;
uniform sampler2D u_shrubInstanceTex;
uniform int u_instanceBaseOffset;

vec4 FetchShrubTexel(int texelIdx)
{
    int tx = texelIdx & 511;
    int ty = texelIdx >> 9;
    return texelFetch(u_shrubInstanceTex, ivec2(tx, ty), 0);
}

void main()
{
    int base = (gl_InstanceID + u_instanceBaseOffset) * 2;
    vec4 t0 = FetchShrubTexel(base);
    vec4 t1 = FetchShrubTexel(base + 1);

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

    float compliance = vertexColor.a;
    if (compliance > 0.001) {
        vec2 windBend = NatureVegetationWindBend(instPos, vec2(phase, compliance), u_time, u_windStrength, 0.72);
        vec2 impactBend = NatureDominantWindImpact(world);
        world.xz += (windBend + impactBend) * (compliance * 0.28);
    }

    gl_Position = mvp * vec4(world, 1.0);
}
