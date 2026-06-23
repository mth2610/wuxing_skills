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

// 1. Phình/Thắt (Tạo khối nước cuộn to nhỏ theo chiều dọc)
    float swell = sin(t * 10.0 - u_time * 12.0) * cos(t * 4.0 - u_time * 6.0);
    
    // 2. Gồ ghề bề mặt (CHỈNH SỬA Ở ĐÂY)
    // Tách phi và t ra thành 2 hàm nhân với nhau. 
    // Các u cục sẽ nổi lên hạ xuống tại chỗ hoặc trượt dọc, TUYỆT ĐỐI KHÔNG XOẮN.
    float bump = sin(phi * 4.0 + u_time * 5.0) * cos(t * 14.0 - u_time * 10.0);
    
    // 3. Xoắn (Đã tắt theo ý bạn)
    float gentleTwist = 0.0; 
    
    // Tỷ lệ mới: Tăng mạnh độ gồ ghề (bump) và phình (swell)
    float irregularity = swell * 0.50 + bump * 0.50 + gentleTwist;
    
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    
    // Lực đẩy
    float displacement = irregularity * dampen * 4.0;

    vec3 displacedPos = vertexPosition + vertexNormal * displacement;
    
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    
    gl_Position = mvp * vec4(displacedPos, 1.0);
}