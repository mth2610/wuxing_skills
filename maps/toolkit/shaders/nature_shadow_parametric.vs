#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;

uniform mat4 mvp;
uniform vec3 u_worldOffset;
uniform float u_time;
uniform float u_windStrength;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

out vec2 fragTexCoord;

#define NATURE_BLADE_POSITION_ONLY
#include "maps/toolkit/shaders/nature_parametric.glsl"

void main()
{
    vec3 bladePosition, bladeNormal;
    vec4 bladeColor;
    vec2 bladeWindUV, bladeSurfaceUV;
    NatureEvaluateBlade(bladePosition, bladeNormal, bladeColor, bladeWindUV, bladeSurfaceUV);

    vec3 local = bladePosition;
    float h = bladeWindUV.y;

    // Bỏ qua tính toán gió & đọc texture tương tác cho các đỉnh gốc cỏ đứng yên (h == 0)
    if (h > 0.0001) {
        vec3 rootLocal = s_cachedClumpTexel.xyz;
        vec3 rootWorld = rootLocal + u_worldOffset;
        float rootMask = h * (0.42 + 0.58 * h);
        float maxAllowedBend = max(u_interactionMaxBend * u_natureWindResponse.w, 0.001);

        vec2 windBend = NatureVegetationWindBend(
            rootWorld, bladeWindUV, u_time, u_windStrength, maxAllowedBend);
        windBend += NatureDominantWindImpact(rootWorld) * u_natureWindResponse.w;

        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (rootWorld.xz - u_interactionCenter) / u_interactionWorldSize;
            float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));
            if (maxAxis < 0.5) {
                vec2 interactionUV = uvOffset + 0.5;
                vec3 sampleValue = textureLod(u_interactionMap, interactionUV, 0.0).rgb;
                vec2 pushDirection = sampleValue.rg * 2.0 - 1.0;
                float edgeFeather = clamp((0.50 - maxAxis) * 12.5, 0.0, 1.0);
                edgeFeather *= edgeFeather * (3.0 - 2.0 * edgeFeather);
                windBend += pushDirection * (sampleValue.b * edgeFeather * maxAllowedBend);
            }
        }

        // Gộp bước giới hạn biên độ uốn và độ hạ thấp ngọn cỏ đồng bộ với nature_lit_parametric.vs
        float rawBendSq = dot(windBend, windBend);
        float rawBendLen = (rawBendSq > 1e-10) ? (rawBendSq * inversesqrt(rawBendSq)) : 0.0;
        if (rawBendLen > maxAllowedBend) {
            windBend *= maxAllowedBend / rawBendLen;
            rawBendLen = maxAllowedBend;
        }

        float dispLen = rawBendLen * rootMask;
        local.xz += windBend * rootMask;
        local.y  -= dispLen * 0.18 * h;
    }

    fragTexCoord = bladeSurfaceUV;
    gl_Position = mvp * vec4(local, 1.0);
}