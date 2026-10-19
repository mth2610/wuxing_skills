#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

out vec3 v_worldPos;
out vec2 v_texCoord;

uniform mat4 mvp;

void main() {
    v_texCoord = vertexTexCoord;
    v_worldPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
