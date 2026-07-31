#version 430

layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec2 vertexTexCoord;

struct Particle {
    vec4 pos_radius;
    vec4 vel_drag;
    vec4 force_turb;
    vec4 colorStart;
    vec4 colorEnd;
    vec4 lifeData;
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

uniform mat4 mvp;
uniform mat4 matView; // Dùng để trích xuất Billboard (luôn hướng về Camera)

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
    // Lấy dữ liệu của hạt hiện tại bằng gl_InstanceID
    Particle p = particles[gl_InstanceID];
    
    // Nếu hạt đã chết, bóp nó thành 1 điểm tàng hình ở tọa độ 0
    if (p.lifeData.w < 0.5) {
        gl_Position = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Tính toán tỷ lệ sống để hòa trộn màu (tương tự mã C cũ của bạn)
    float lifeRatio = p.lifeData.x / p.lifeData.y;
    vec4 mixedColor = mix(p.colorEnd, p.colorStart, lifeRatio);
    // Áp dụng fade out alpha theo đường cong (lifeRatio^2)
    mixedColor.a = mix(p.colorEnd.a, p.colorStart.a, lifeRatio * lifeRatio);
    fragColor = mixedColor;
    fragTexCoord = vertexTexCoord;

    // Billboard Math: Xoay hình vuông hướng thẳng vào Camera
    vec3 cameraRight = vec3(matView[0][0], matView[1][0], matView[2][0]);
    vec3 cameraUp    = vec3(matView[0][1], matView[1][1], matView[2][1]);
    
    float radius = p.pos_radius.w;
    
    // Scale vertexPosition cơ bản bằng camera right/up
    vec3 localPos = cameraRight * vertexPosition.x * radius + cameraUp * vertexPosition.y * radius;
    vec3 finalPos = p.pos_radius.xyz + localPos;

    gl_Position = mvp * vec4(finalPos, 1.0);
}