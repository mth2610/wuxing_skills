#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out vec3 fragNormal;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    // QUAN TRỌNG: vertexPosition ở đây ĐÃ LÀ world-space tuyệt đối, vì
    // RenderShieldSphere() trong shield_skill.c tự cộng center + n*radius
    // trên CPU trước khi đẩy vào rlVertex3f (không dùng model matrix riêng).
    // Vì vậy KHÔNG nhân thêm matModel ở đây - chỉ nhân mvp cho gl_Position.
    // Nếu sau này đổi sang dùng DrawMesh()/model matrix thì chỗ này phải
    // sửa lại thành mat3(matModel) * vertexNormal và (matModel *
    // vec4(vertexPosition,1.0)).xyz tương ứng.
    fragPosition = vertexPosition;
    fragNormal = normalize(vertexNormal);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
