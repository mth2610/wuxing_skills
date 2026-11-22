#version 330
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor; // rgb = vertex tint (trunk vs canopy), a = wind compliance (0=base, 1=outer foliage)

uniform mat4 mvp;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_treeInstanceTex;
uniform float u_treeDrawDistSq;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor; // rgb = vertex color * instance tint, a = canopy mask (texCoord.y)
out vec2 fragTexCoord;
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
    int base = gl_InstanceID * 2;
    vec4 t0 = FetchTreeTexel(base);
    vec4 t1 = FetchTreeTexel(base + 1);

    vec3 instPos = t0.xyz;
    float rotRad = t0.w;
    float scaleXZ = t1.x;
    float scaleY = t1.y;
    float phase = t1.z;
    int packedRGB = int(t1.w + 0.5);

    vec3 toCam = instPos - u_viewPos;
    if (dot(toCam, toCam) > u_treeDrawDistSq) {
        fragPosition = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragColor = vec4(0.0);
        fragTexCoord = vec2(0.0);
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

    // 3-level hierarchical wind sway: trunk bend -> branch sway -> leaf flutter
    float hNorm = clamp(lp.y / max(scaleY * 4.5, 1.0), 0.0, 1.0);
    float compliance = vertexColor.a;
    if (compliance > 0.001 && u_windStrength > 0.001) {
        vec2 windBend = NatureVegetationWindBend(instPos, vec2(phase, hNorm), u_time, u_windStrength, 0.85);
        float trunkSway = hNorm * hNorm * 0.45;
        float leafFlutter = sin(u_time * 3.8 + phase * 6.283 + lp.x * 2.1 + lp.z * 2.3) * 0.045 * compliance;
        world.xz += windBend * trunkSway + vec2(leafFlutter, leafFlutter * 0.7);
        world.y -= dot(windBend, windBend) * 0.08 * hNorm;
    }

    int r = (packedRGB >> 16) & 255;
    int g = (packedRGB >> 8) & 255;
    int b = packedRGB & 255;
    vec3 tint = vec3(float(r), float(g), float(b)) * (1.0 / 255.0);

    fragPosition = world;
    fragNormal = wn * inversesqrt(max(dot(wn, wn), 1e-8));
    // vertexTexCoord.y: 0.0 = bark/trunk, 1.0 = foliage/blossom canopy
    fragColor = vec4(mix(vertexColor.rgb, vertexColor.rgb * tint, vertexTexCoord.y), vertexTexCoord.y);
    fragTexCoord = vertexTexCoord;

    if (dot(toCam, toCam) <= 45.0 * 45.0) {
        vec4 world4 = vec4(world, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    gl_Position = mvp * vec4(world, 1.0);
}
