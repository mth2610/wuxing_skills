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

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    vec4 worldVertex = vec4(vertexPosition, 1.0);
    vec4 shaderPos = matModel * worldVertex;
    fragPosition = shaderPos.xyz;
    fragNormal   = vertexNormal;
    fragWorldPos = vertexPosition + u_groundOffset;
    v_lightSpace = u_lightVP * shaderPos;
    v_staticLightSpace = u_staticLightVP * shaderPos;
    gl_Position  = mvp * worldVertex;
}