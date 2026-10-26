#version 330

// Đợt E / E2 — added so ground_splat.fs can be lit by VFX point lights.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 u_groundOffset;
uniform mat4 u_lightVP;
uniform mat4 u_staticLightVP;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec3 fragNormal;
out vec3 fragWorldPos;
out vec4 v_lightSpace;
out vec4 v_staticLightSpace;
out vec2 v_macroPatchAndField;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    vec4 worldVertex = vec4(vertexPosition, 1.0);
    vec4 shaderPos = matModel * worldVertex;
    fragPosition = shaderPos.xyz;

    // Chuẩn hóa pháp tuyến an toàn theo ma trận model tại Vertex Shader
    vec3 worldNormal = mat3(matModel) * vertexNormal;
    float nLenSq = dot(worldNormal, worldNormal);
    fragNormal = (nLenSq > 1e-8) ? (worldNormal * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 wp      = vertexPosition + u_groundOffset;
    fragWorldPos = wp;
    v_lightSpace = u_lightVP * shaderPos;
    v_staticLightSpace = u_staticLightVP * shaderPos;

    float patch = sin(wp.x * 0.12 + wp.z * 0.08) * 0.55
                + cos(wp.x * -0.07 + wp.z * 0.16) * 0.45;
    float macroA = sin(wp.x * 0.042 + wp.z * 0.028);
    float macroB = sin(wp.x * -0.022 + wp.z * 0.048 + 1.35);
    float field = 0.50 + 0.32 * macroA + 0.18 * macroB;
    v_macroPatchAndField = vec2(patch, field);

    gl_Position  = mvp * worldVertex;
}
