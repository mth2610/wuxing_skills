#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord; // x = normalized height/radial progress [0..1], y = part mask (0=bark, 0.5=accent/thorn, 1.0=foliage)
in vec3 vertexNormal;
in vec4 vertexColor;    // rgb = vertex botanical color, a = wind compliance (0=root/wood, 1=leaf tip)

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
uniform sampler2D u_shrubInstanceTex;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;     // rgb = tinted vertex color, a = part mask (vertexTexCoord.y)
out vec4 v_shrubParams; // x = rootHeight, y = dryness, z = scorch, w = frost
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

vec4 FetchShrubTexel(int texelIdx)
{
    int tx = texelIdx & 511;
    int ty = texelIdx >> 9;
    return texelFetch(u_shrubInstanceTex, ivec2(tx, ty), 0);
}

void main()
{
    int base = gl_InstanceID * 2;
    vec4 t0 = FetchShrubTexel(base);
    vec4 t1 = FetchShrubTexel(base + 1);

    vec3 instPos = t0.xyz + u_worldOffset;
    float rotRad = t0.w;
    float scaleXZ = t1.x;
    float scaleY = t1.y;

    float packedState = t1.z;
    float frostQ = floor(packedState * (1.0 / 4096.0));
    float rem0 = packedState - frostQ * 4096.0;
    float scorchQ = floor(rem0 * (1.0 / 256.0));
    float rem1 = rem0 - scorchQ * 256.0;
    float dryQ = floor(rem1 * (1.0 / 16.0));
    float phase = clamp(rem1 - dryQ * 16.0, 0.0, 0.999);

    float dryness = clamp(dryQ * (1.0 / 15.0), 0.0, 1.0);
    float scorch  = clamp(scorchQ * (1.0 / 15.0), 0.0, 1.0);
    float frost   = clamp(frostQ * (1.0 / 15.0), 0.0, 1.0);

    int packedRGB = int(floor(t1.w + 0.5));
    vec3 foliageTint = vec3(
        float((packedRGB >> 16) & 255),
        float((packedRGB >> 8) & 255),
        float(packedRGB & 255)
    ) * (1.0 / 255.0);

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

    float compliance = vertexColor.a;
    if (compliance > 0.001) {
        float hNorm = clamp(vertexTexCoord.x, 0.0, 1.0);
        float maxAllowedBend = min(max(u_interactionMaxBend * 0.72, 0.04), max(scaleY * 0.45, 0.18));
        vec2 windBend = NatureVegetationWindBend(
            instPos, vec2(phase, hNorm), u_time, u_windStrength * compliance * (1.0 - scorch * 0.5), maxAllowedBend);
        windBend += NatureDominantWindImpact(instPos) * (0.75 * compliance);

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (instPos.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            if (maxAxis < 0.5) {
                vec2 interactionUV = uvOffset + 0.5;
                vec3 sampleValue = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                vec2 pushDirection = sampleValue.rg * 2.0 - 1.0;
                float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
                edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
                windBend += pushDirection * (sampleValue.b * edgeFeather * maxAllowedBend * compliance);
            }
        }

        float flutter = sin(mod(u_time * 4.5, 6283.185307) + phase * 6.2831853 + lp.x * 3.1 + lp.z * 2.7) *
                        (0.014 * min(u_windStrength, 0.25) + 0.004) * compliance;
        float bendFactor = hNorm * (0.35 + 0.65 * hNorm);
        world.xz += windBend * bendFactor + vec2(flutter, flutter * 0.7);
        world.y -= dot(windBend, windBend) * 0.14 * bendFactor;
    }

    float partMask = vertexTexCoord.y;
    float foliageWeight = smoothstep(0.55, 1.0, partMask);
    vec3 tintedCol = mix(vertexColor.rgb, vertexColor.rgb * foliageTint, foliageWeight);

    fragPosition = world;
    fragNormal = wn * inversesqrt(max(dot(wn, wn), 1e-8));
    fragColor = vec4(tintedCol, partMask);
    v_shrubParams = vec4(clamp(vertexTexCoord.x, 0.0, 1.0), dryness, scorch, frost);

    vec3 toCam = world - u_viewPos;
    if (dot(toCam, toCam) <= 48.0 * 48.0) {
        vec4 world4 = vec4(world, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0, 0.0, 0.0, -1.0);
        v_staticLightSpace = vec4(0.0, 0.0, 0.0, -1.0);
    }

    gl_Position = mvp * vec4(world, 1.0);
}
