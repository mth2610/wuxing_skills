#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"
#include "maps/toolkit/shaders/grass_blade_state.glsl"

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
out vec4 fragColor; // .rgb = blade color, .a = dynamic wind sheen factor
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
    float windSheen = 0.0;

    // Skip all wind & state deformation evaluations for stationary root vertices (h == 0)
    if (h > 0.0001) {
        float windCompliance = 1.0;
        vec2 grassState = GrassEvaluateLocalState(world);
        if (dot(grassState, grassState) > 1e-5) {
            vec3 rootLocal = s_hasCachedClumpTexel ? s_cachedClumpTexel.xyz : vec3(local.x, local.y - h, local.z);
            GrassApplyVertexMorphology(local, rootLocal, h, grassState, windCompliance);
            world = local + u_worldOffset;
        }

        float rootMask = h * h;
        float maxAllowedBend = max(u_interactionMaxBend * u_natureWindResponse.w * windCompliance, 0.001);

        vec2 windBend = NatureVegetationWindBend(
            world, bladeWindUV, u_time, u_windStrength * windCompliance, maxAllowedBend);
        windBend += NatureDominantWindImpact(world) * (u_natureWindResponse.w * windCompliance);

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (world.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            // Only sample interaction texture when vertex is inside the active region
            if (maxAxis < 0.5) {
                vec2 interactionUV = uvOffset + 0.5;
                vec3 interactionSample = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                vec2 pushDirection = interactionSample.rg * 2.0 - 1.0;
                float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
                edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
                windBend += pushDirection * (interactionSample.b * edgeFeather * maxAllowedBend);
            }
        }

        vec2 dispXZ = windBend * rootMask;
        float dispSq = dot(dispXZ, dispXZ);
        float dispLen = sqrt(dispSq);

        // Elevation-independent arc radius: fixes stretching on hills and clamping in valleys,
        // while using Padé approximation with dispLen to avoid a second sqrt() call.
        float arcRadius = max(maxAllowedBend * 1.35 * max(h, 0.22), 0.16);
        local.xz += dispXZ;
        local.y  -= dispSq / (2.0 * arcRadius + 0.42 * dispLen);

        world = local + u_worldOffset;

        // As the blade bends over, its upper surface tilts skyward (+Y) and along the bend slope,
        // creating natural rolling specular wind waves instead of dark inverted patches.
        bentNormal.xz += windBend * (0.95 * h);
        bentNormal.y  += dispLen * 0.45;

        // Window windSheen to the broad mid-upper arch (tapers to 0 at tip h > 0.85 so tips never flicker)
        float archSheenMask = h * clamp((0.92 - h) * 4.5, 0.0, 1.0);
        windSheen = clamp(dispLen / maxAllowedBend, 0.0, 1.0) * archSheenMask;
    }

    fragPosition = world;
    fragNormal = normalize(bentNormal);
    fragColor = vec4(bladeColor.rgb, windSheen);
    fragHeight = h;
    fragTexCoord = bladeSurfaceUV;
    v_lightSpace = u_lightVP * vec4(world, 1.0);
    v_staticLightSpace = u_staticLightVP * vec4(world, 1.0);
    gl_Position = mvp * vec4(local, 1.0);
}