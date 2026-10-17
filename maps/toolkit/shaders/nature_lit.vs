#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"
#include "maps/toolkit/shaders/grass_blade_state.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;

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

void main()
{
    vec3 local = vertexPosition;
    vec3 shaderPosition = vec3(matModel * vec4(local, 1.0));
    vec3 world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));

    // Precompute combined 3x3 linear basis once (reused for displacement & normal)
    mat3 worldLinear = mat3(u_worldFromShaderSpace) * mat3(matModel);
    float h = vertexTexCoord.y;
    vec3 bentNormal = vertexNormal;
    float windSheen = 0.0;

    // Skip wind & state evaluations for stationary root vertices
    if (h > 0.0001) {
        float windCompliance = 1.0;
        vec2 grassState = GrassEvaluateLocalState(world);
        if (dot(grassState, grassState) > 1e-5) {
            vec3 rootApprox = vec3(local.x, local.y - h, local.z);
            GrassApplyVertexMorphology(local, rootApprox, h, grassState, windCompliance);
            shaderPosition = vec3(matModel * vec4(local, 1.0));
            world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));
        }

        float rootMask = h * h;
        float maxAllowedBend = max(u_interactionMaxBend * u_natureWindResponse.w * windCompliance, 0.001);

        vec2 windBend = NatureVegetationWindBend(
            world, vertexTexCoord, u_time, u_windStrength * windCompliance, maxAllowedBend);
        windBend += NatureDominantWindImpact(world) * (u_natureWindResponse.w * windCompliance);

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (world.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            if (maxAxis < 0.5) {
                vec2 interactionUV = uvOffset + 0.5;
                vec3 interactionSample = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                vec2 pushDirection = interactionSample.rg * 2.0 - 1.0;
                float edgeFeather = 1.0 - smoothstep(0.42, 0.50, maxAxis);
                float interaction = interactionSample.b * edgeFeather;
                windBend += pushDirection * (interaction * maxAllowedBend);
            }
        }

        vec2 dispXZ = windBend * rootMask;
        float dispSq = dot(dispXZ, dispXZ);
        float dispLen = sqrt(dispSq);
        float effHeight = max(abs(local.y), 0.15);
        float dispY = -dispSq / (effHeight * 1.8 + sqrt(effHeight * effHeight * 3.24 + dispSq));

        vec3 localDelta = vec3(dispXZ.x, dispY, dispXZ.y);
        local += localDelta;
        world += worldLinear * localDelta;

        bentNormal.xz += windBend * (0.95 * h);
        bentNormal.y  += dispLen * 0.45;

        float archSheenMask = h * clamp((0.92 - h) * 4.5, 0.0, 1.0);
        windSheen = clamp(dispLen / maxAllowedBend, 0.0, 1.0) * archSheenMask;
    }

    fragPosition = world;
    fragNormal = normalize(worldLinear * bentNormal);
    fragColor = vec4(vertexColor.rgb, windSheen);
    fragHeight = h;
    fragTexCoord = vertexTexCoord2;
    v_lightSpace = u_lightVP * vec4(world, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(world, 1.0);
    gl_Position = mvp * vec4(local, 1.0);
}