#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform vec3 u_modelPos;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec4 fragColor;

void main() {
    fragPosition = vertexPosition + u_modelPos;
    fragTexCoord = vertexTexCoord;
    float nLenSq = dot(vertexNormal, vertexNormal);
    fragNormal = (nLenSq > 1e-8) ? (vertexNormal * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);
    fragColor = vertexColor;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
