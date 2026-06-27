#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;
uniform float u_uvLength; 

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    float t = vertexTexCoord.y / u_uvLength; 
    float phi = vertexTexCoord.x * 6.28318;

    // 1. Phình/Thắt: Kéo giãn nhịp (t * 4.0 thay vì 12.0) để tạo luồng nước dài
    float swell = sin(t * 4.0 - u_time * 12.0);
    
    // 2. Múi nước (Bump): Kéo dài múi nước ra (t * 8.0 thay vì 24.0)
    // Tốc độ trượt cực nhanh (u_time * 20.0)
    float bump = sin(t * 8.0 - u_time * 20.0) * cos(phi * 2.0);
    
    // Trộn 50/50 để các múi nước hòa quyện êm ái
    float irregularity = swell * 0.5 + bump * 0.5;
    
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    
    // Cường độ đẩy gồ ghề (4.0 là đủ vì múi giờ đã rất to)
    float displacement = irregularity * dampen * 4.0; 

    vec3 displacedPos = vertexPosition + vertexNormal * displacement;
    
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    
    gl_Position = mvp * vec4(displacedPos, 1.0);
}