#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 u_worldFromShaderSpace;
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

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out float fragHeight;
out vec2 fragTexCoord;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;

#define NATURE_VISIBLE_TUFT_LOD
#include "maps/toolkit/shaders/nature_parametric.glsl"

void main()
{
    // Compact submissions contain only this LOD's roots. The diagnostic
    // legacy path clips rejected templates before blade/wind evaluation.
    if (!NatureTuftUsesCurrentLod()) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec3 bladePosition, bladeNormal;
    vec4 bladeColor;
    vec2 bladeWindUV, bladeSurfaceUV;
    NatureEvaluateBlade(bladePosition, bladeNormal, bladeColor, bladeWindUV, bladeSurfaceUV);
    vec3 local = bladePosition;
    vec3 shaderPosition = vec3(matModel * vec4(local, 1.0));
    vec3 world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));
    float rootMask = bladeWindUV.y * bladeWindUV.y;
    vec2 windBend = NatureVegetationWindBend(
        world, bladeWindUV, u_time, u_windStrength,
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
    vec3 bentNormal = bladeNormal;
    bentNormal.xz -= windBend * 1.15 * bladeWindUV.y;

    fragPosition = world;
    fragNormal = normalize(mat3(u_worldFromShaderSpace) * mat3(matModel) * bentNormal);
    fragColor = bladeColor;
    fragHeight = bladeWindUV.y;
    fragTexCoord = bladeSurfaceUV;
    v_lightSpace = u_lightVP * vec4(world, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(world, 1.0);
    gl_Position = mvp * vec4(local, 1.0);
}
