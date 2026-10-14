#version 330

// Đợt E / E2 — added so ground_splat.fs can be lit by VFX point lights.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 u_groundOffset;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec3 fragNormal;
out vec3 fragWorldPos;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    vec4 worldVertex = vec4(vertexPosition, 1.0);
    fragPosition = (matModel * worldVertex).xyz;
    fragNormal   = vertexNormal;
    fragWorldPos = vertexPosition + u_groundOffset;
    gl_Position  = mvp * worldVertex;
}