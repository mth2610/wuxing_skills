#version 330 core
// Vertex shader — CPU/VBO PATH (macOS GL 3.3 fallback)
// VBO layout: position(vec3) | texcoord(vec2) | color(vec4 float) = 36 bytes/vert
// Billboard corners đã được tính trên CPU và upload vào VBO mỗi frame.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
}
