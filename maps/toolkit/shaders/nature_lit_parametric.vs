#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;

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
    if (!NatureTuftUsesCurrentLod()) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 bladePosition, bladeNormal;
    vec4 bladeColor;
    vec2 bladeWindUV, bladeSurfaceUV;
    NatureEvaluateBlade(bladePosition, bladeNormal, bladeColor, bladeWindUV, bladeSurfaceUV);

    // Early clip if perimeter fade collapsed the blade completely
    if (bladeColor.a <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 local = bladePosition;
    vec3 world = local + u_worldOffset;
    float h = bladeWindUV.y;
    vec3 bentNormal = bladeNormal;

    // Skip all wind & texture evaluations for stationary root vertices (h == 0)
    if (h > 0.0001) {
        float rootMask = h * h;
        float maxAllowedBend = u_interactionMaxBend * u_natureWindResponse.w;

        vec2 windBend = NatureVegetationWindBend(
            world, bladeWindUV, u_time, u_windStrength, maxAllowedBend);
        windBend += NatureDominantWindImpact(world) * u_natureWindResponse.w;

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (world.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            // Only sample interaction texture when vertex is inside the active region
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
        local.xz += dispXZ;

        // Length-preserving vertical arc depression so blades bow down instead of stretching
        float dispSq = dot(dispXZ, dispXZ);
        float effHeight = max(local.y, 0.12);
        local.y -= dispSq / (effHeight * 1.6 + sqrt(effHeight * effHeight * 2.56 + dispSq));

        world = local + u_worldOffset;

        // Dynamic normal tilt + vertical curvature compensation for natural specular waves
        bentNormal.xz -= windBend * (1.15 * h);
        bentNormal.y  -= length(dispXZ) * 0.35;
    }

    fragPosition = world;
    fragNormal = normalize(bentNormal);
    fragColor = vec4(bladeColor.rgb, 1.0);
    fragHeight = h;
    fragTexCoord = bladeSurfaceUV;
    v_lightSpace = u_lightVP * vec4(world, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(world, 1.0);
    gl_Position = mvp * vec4(local, 1.0);
}