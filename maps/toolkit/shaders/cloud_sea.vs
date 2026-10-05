#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
uniform mat4 mvp;
uniform vec3 u_cloudOffset;
out vec2 fragTexCoord;
out vec3 fragWorldPos;
out vec3 fragNormal;
void main()
{
    fragTexCoord = vertexTexCoord;
    fragWorldPos = vertexPosition + u_cloudOffset; // Translation-only; matModel contains view.
    fragNormal = vertexNormal; // Cloud models use translation only.
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
