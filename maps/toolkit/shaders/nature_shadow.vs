#version 330
#include "maps/toolkit/shaders/nature_wind_impact.glsl"
#include "maps/toolkit/shaders/nature_wind_field.glsl"

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 u_worldFromShaderSpace;
uniform float u_time;
uniform float u_windStrength;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

out vec2 fragTexCoord;

void main()
{
    vec3 local = vertexPosition;
    float h = vertexTexCoord.y;

    // [TỐI ƯU HIỆU NĂNG]: Bỏ qua 2 phép nhân ma trận mat4, nhiễu gió 3D và đọc texture
    // cho các đỉnh gốc đứng yên (h == 0) vì rootMask == 0 không làm thay đổi local.xz
    if (h > 0.0001) {
        vec3 shaderPosition = vec3(matModel * vec4(local, 1.0));
        vec3 world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0));
        float rootMask = h * h;
        float maxAllowedBend = max(u_interactionMaxBend * u_natureWindResponse.w, 0.001);

        vec2 windBend = NatureVegetationWindBend(
            world, vertexTexCoord, u_time, u_windStrength, maxAllowedBend);
        windBend += NatureDominantWindImpact(world) * u_natureWindResponse.w;

        // [VULKAN / SPIR-V FIX & BANDWIDTH OPT]: Dùng textureLod(..., 0.0) thay vì texture()
        // trong Vertex Shader và chỉ đọc texture khi đỉnh nằm trong phạm vi tương tác
        if (u_interactionEnabled != 0 && u_interactionWorldSize > 0.0001) {
            vec2 uvOffset = (world.xz - u_interactionCenter) / u_interactionWorldSize;
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

        // Giới hạn tổng biên độ uốn đồng bộ với nature_lit.vs
        float rawBendSq = dot(windBend, windBend);
        if (rawBendSq > maxAllowedBend * maxAllowedBend) {
            windBend *= maxAllowedBend * inversesqrt(rawBendSq);
        }

        local.xz += windBend * rootMask;
    }

    fragTexCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(local, 1.0);
}
