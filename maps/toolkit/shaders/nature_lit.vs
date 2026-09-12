#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 u_worldFromShaderSpace;
uniform float u_time;
uniform float u_windStrength;
uniform vec3 u_viewPos;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out float fragHeight;
out vec2 fragTexCoord;

void main()
{
    vec3 local = vertexPosition;
    vec3 shaderPosition = vec3(matModel * vec4(local, 1.0));
    vec3 world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));
    float rootMask = vertexTexCoord.y * vertexTexCoord.y;
    vec2 windBend = NatureVegetationWindBend(
        world, vertexTexCoord, u_time, u_windStrength,
        u_interactionMaxBend * u_natureWindResponse.w);
    windBend += NatureDominantWindImpact(world) * u_natureWindResponse.w;

    if (u_interactionEnabled != 0) {
        vec2 interactionUV = (world.xz - u_interactionCenter) / u_interactionWorldSize + 0.5;
        vec2 inside = step(vec2(0.0), interactionUV) * step(interactionUV, vec2(1.0));
        vec3 interactionSample = texture(u_interactionMap, clamp(interactionUV, 0.0, 1.0)).rgb;
        vec2 pushDirection = interactionSample.rg * 2.0 - 1.0;
        float interaction = interactionSample.b * inside.x * inside.y;
        windBend += pushDirection * interaction * u_interactionMaxBend *
                    u_natureWindResponse.w;
    }
    local.xz += windBend * rootMask;
    shaderPosition = vec3(matModel * vec4(local, 1.0));
    world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));

    // Normal tilts dynamically with wind deflection, creating iconic specular ripples
    vec3 bentNormal = vertexNormal;
    bentNormal.xz -= windBend * 1.15 * vertexTexCoord.y;

    fragPosition = shaderPosition;
    fragNormal = normalize(mat3(matModel) * bentNormal);
    fragColor = vertexColor;
    fragHeight = vertexTexCoord.y;
    fragTexCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(local, 1.0);
}
