#version 330
#include "maps/toolkit/shaders/seismic_displacement.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform vec3 u_viewPos;
uniform sampler2D u_debrisInstanceTex;
uniform float u_debrisDrawDistSq;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out float fragHeight;
out vec2 fragTexCoord;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;
out float v_macroPatch;

vec4 FetchDebrisTexel(int texelIdx)
{
    int tx = texelIdx & 511;
    int ty = texelIdx >> 9;
    return texelFetch(u_debrisInstanceTex, ivec2(tx, ty), 0);
}

void main()
{
    int base = gl_InstanceID * 2;
    vec4 t0 = FetchDebrisTexel(base);
    vec4 t1 = FetchDebrisTexel(base + 1);

    vec3 instPos = t0.xyz;
    float rotRad = t0.w;
    float scale = t1.x;
    int packedRGB = int(t1.y + 0.5);

    vec3 toCam = instPos - u_viewPos;
    if (dot(toCam, toCam) > u_debrisDrawDistSq) {
        fragPosition = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragColor = vec4(0.0);
        fragHeight = 0.0;
        fragTexCoord = vec2(0.0);
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_macroPatch = 0.0;
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    float c = cos(rotRad);
    float s = sin(rotRad);
    vec3 lp = vertexPosition * scale;
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

    vec2 seismicSlope = vec2(0.0);
    float seismicDY = EvaluateSeismicDisplacement(world.xz, seismicSlope);
    if (abs(seismicDY) > 1e-5 || dot(seismicSlope, seismicSlope) > 1e-8) {
        world.y += seismicDY;
        wn = PerturbNormalWithSeismicWave(wn, seismicSlope);
    }

    int r = (packedRGB >> 16) & 255;
    int g = (packedRGB >> 8) & 255;
    int b = packedRGB & 255;
    vec3 tint = vec3(float(r), float(g), float(b)) * (1.0 / 255.0);

    fragPosition = world;
    fragNormal = wn * inversesqrt(max(dot(wn, wn), 1e-8));
    fragColor = vec4(vertexColor.rgb * tint, 0.0);
    fragHeight = 0.0;
    fragTexCoord = vertexTexCoord2;

    if (dot(toCam, toCam) <= 36.5 * 36.5) {
        vec4 world4 = vec4(world, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    float warpX = instPos.x + sin(instPos.z * 0.08) * 3.5;
    float warpZ = instPos.z + cos(instPos.x * 0.07) * 3.2;
    v_macroPatch = sin(warpX * 0.055 + warpZ * 0.038) * 0.55 +
                   sin(warpZ * 0.11 - warpX * 0.045 + 1.2) * 0.30 +
                   sin(warpX * 0.19 + warpZ * 0.15) * 0.15;

    gl_Position = mvp * vec4(world, 1.0);
}
