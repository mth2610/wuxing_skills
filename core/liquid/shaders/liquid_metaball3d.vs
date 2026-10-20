#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

out vec3 v_worldPos;
out vec2 v_texCoord;

uniform mat4 mvp;
uniform mat4 u_invView;

void main() {
    v_texCoord = vertexTexCoord;
    v_worldPos = vec3(u_invView * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
