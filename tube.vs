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

    // KÉO GIÃN TẦN SỐ SÓNG: Dùng các con số nhỏ hơn để tạo nhịp sóng dài, mượt
    float swell = sin(t * 6.0 - u_time * 10.0) * cos(t * 2.0 - u_time * 4.0);
    // Thay vì u cục li ti, đây sẽ là những gân nước dài trượt trên bề mặt
    float bump = sin(phi * 2.0 + u_time * 4.0) * cos(t * 8.0 - u_time * 8.0);
    
    // Tỷ lệ trộn: Sóng dài áp đảo
    float irregularity = swell * 0.6 + bump * 0.4;
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    
    // Ép form mượt mà hơn
    float displacement = irregularity * dampen * 3.5; 

    vec3 displacedPos = vertexPosition + vertexNormal * displacement;
    
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    
    gl_Position = mvp * vec4(displacedPos, 1.0);
}