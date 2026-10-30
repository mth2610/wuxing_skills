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
out float v_macroPatch;

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
        vec3 rootLocal = s_cachedClumpTexel.xyz;
        vec3 rootWorld = rootLocal + u_worldOffset;
        vec3 spineCenter = local - s_cachedSide * (s_cachedHalfWidth * vertexPosition.x);

        float windCompliance = 1.0;
        if ((u_grassStateParams.x + u_grassStateParams.y) > 0.0001 || u_grassBurnZoneCount > 0) {
            vec2 grassState = GrassEvaluateLocalState(rootWorld);
            if (dot(grassState, grassState) > 1e-5) {
                GrassApplyVertexMorphology(spineCenter, rootLocal, h, grassState, windCompliance);
            }
        }

        float bladeSpanY = max(spineCenter.y - rootLocal.y, 0.0);
        float bladeHeightEst = max(bladeSpanY / max(h, 0.25), 0.22);
        float rootMask = h * (0.42 + 0.58 * h);
        float maxAllowedBend = min(
            max(u_interactionMaxBend * u_natureWindResponse.w * windCompliance, 0.001),
            bladeHeightEst * 0.58);

        vec2 windBend = NatureVegetationWindBend(
            rootWorld, bladeWindUV, u_time, u_windStrength * windCompliance, maxAllowedBend);

        // [TỐI ƯU ALU]: Chỉ tính xung lực gió cục bộ và tương tác dẫm cỏ cho LOD 0 & LOD 1 (< 22m)
        if (u_geometryLod < 2) {
            windBend += NatureDominantWindImpact(rootWorld) * (u_natureWindResponse.w * windCompliance);

            if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
                vec2 uvOffset = (rootWorld.xz - u_interactionCenter) / u_interactionWorldSize;
                float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
                if (maxAxis < 0.5) {
                    vec2 interactionUV = uvOffset + 0.5;
                    vec3 interactionSample = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                    vec2 pushDirection = interactionSample.rg * 2.0 - 1.0;
                    float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
                    edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
                    windBend += pushDirection * (interactionSample.b * edgeFeather * maxAllowedBend);
                }
            }
        }

        // Gộp bước giới hạn biên độ gió và tính dispLen chỉ với 1 lệnh inversesqrt duy nhất
        float rawBendSq = dot(windBend, windBend);
        float rawBendLen = (rawBendSq > 1e-10) ? (rawBendSq * inversesqrt(rawBendSq)) : 0.0;
        if (rawBendLen > maxAllowedBend) {
            windBend *= maxAllowedBend / rawBendLen;
            rawBendLen = maxAllowedBend;
        }

        vec2 dispXZ = windBend * rootMask;
        float dispLen = rawBendLen * rootMask;
        float bendRatio = clamp(dispLen / bladeHeightEst, 0.0, 0.60);
        spineCenter.xz += dispXZ;
        spineCenter.y   = rootLocal.y + bladeSpanY * (1.0 - 0.22 * bendRatio * h);

        if (u_geometryLod < 2) {
            // Trực giao hóa Gram-Schmidt 3D chính xác cho lá cỏ cận cảnh / trung cảnh
            float dMaskDh = 0.45 + 1.10 * h;
            vec3 rawBentTan = s_cachedTangent + vec3(windBend.x * dMaskDh, -dispLen * 0.40, windBend.y * dMaskDh);
            vec3 bentTangent = rawBentTan * inversesqrt(max(dot(rawBentTan, rawBentTan), 1e-8));
            vec3 bentSide = s_cachedSide - bentTangent * dot(s_cachedSide, bentTangent);
            float bsLenSq = dot(bentSide, bentSide);
            bentSide = (bsLenSq > 1e-5) ? (bentSide * inversesqrt(bsLenSq)) : s_cachedSide;
            local = spineCenter + bentSide * (s_cachedHalfWidth * vertexPosition.x);
        } else {
            // Fast-path cho cỏ xa (LOD 2): Tiết kiệm 2 lệnh inversesqrt và 2 tích vô hướng mỗi đỉnh
            local = spineCenter + s_cachedSide * (s_cachedHalfWidth * vertexPosition.x);
        }
        world = local + u_worldOffset;

        bentNormal.xz += windBend * (0.95 * h);
        bentNormal.y  += dispLen * 0.45;

        float archSheenMask = h * clamp((0.90 - h) * 4.5, 0.0, 1.0);
        windSheen = clamp(dispLen / maxAllowedBend, 0.0, 1.0) * archSheenMask;
    }

    fragPosition = world;
    fragNormal = bentNormal * inversesqrt(max(dot(bentNormal, bentNormal), 1e-8));
    fragColor = vec4(bladeColor.rgb, windSheen);
    fragHeight = h;
    fragTexCoord = bladeSurfaceUV;

    // [TỐI ƯU ALU]: Chỉ nhân 2 ma trận bóng đổ 4x4 khi bụi cỏ nằm trong bán kính nhận bóng (<= 28.5m)
    if (s_cachedDistToCam <= 28.5) {
        vec4 world4 = vec4(world, 1.0);
        v_lightSpace = u_lightVP * world4;
        v_staticLightSpace = u_staticLightVP * world4;
    } else {
        v_lightSpace = vec4(0.0);
        v_staticLightSpace = vec4(0.0);
    }

    v_macroPatch = sin(world.x * 0.12 + world.z * 0.08) * 0.55 + cos(world.x * -0.07 + world.z * 0.16) * 0.45;
    gl_Position = mvp * vec4(local, 1.0);
}