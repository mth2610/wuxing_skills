#version 330

// Giữ nguyên tuyệt đối cấu trúc attribute và varying của ground_shadow.vs gốc
// để tránh lệch Location của vertexColor trong chế độ Immediate-Mode (rlBegin) trên rlvk.

in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;

out vec4 fragColor;
out vec3 fragWorldPos;

void main() {
    fragColor = vertexColor;
    fragWorldPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}