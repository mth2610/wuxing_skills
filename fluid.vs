#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_timeVS;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    // Thuật toán bóp méo đỉnh tần số cao (3D Vertex Deformation) làm quả cầu lồi lõm dẻo quẹo
    float wave = sin(vertexPosition.y * 4.5 + u_timeVS * 9.0) * 0.15
               + cos(vertexPosition.x * 3.5 - u_timeVS * 7.0) * 0.12
               + sin(vertexPosition.z * 5.0 + u_timeVS * 11.0) * 0.08;

    // Kéo dãn nhẹ quả cầu theo phương chuyển động thẳng đứng giả lập lực quán tính chất lỏng
    vec3 displacedPos = vertexPosition + vertexNormal * wave;

    // Ép ma trận chuyển đổi tọa độ world-space thực thụ sang Fragment Shader
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;

    gl_Position = mvp * vec4(displacedPos, 1.0);
}