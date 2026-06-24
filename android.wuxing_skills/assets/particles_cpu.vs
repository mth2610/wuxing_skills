#version 330

// Trên macOS (OpenGL 4.1 / GLSL 330), không có SSBO.
// CPU tính billboard positions và truyền qua vertex attributes.

in vec3 vertexPosition;   // Billboard corner position (đã tính trên CPU)
in vec2 vertexTexCoord;   // UV mapping
in vec4 vertexColor;      // Màu particle (đã mix trên CPU theo lifeRatio)

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
