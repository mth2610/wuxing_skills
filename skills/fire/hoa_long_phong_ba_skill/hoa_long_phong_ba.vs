#version 330

#ifdef GL_ES
precision highp float;
#endif

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

    // 1. Phình/Thắt khí động học: Tần số nhanh hơn nước để thể hiện ngọn lửa dữ dội
    float swell = sin(t * 10.0 - u_time * 18.0) * 0.6 + cos(t * 5.0 - u_time * 10.0) * 0.4;
    
    // 2. Múi lửa xoắn ốc (Spiral bumps) cuộn quanh thân ống mesh
    float bump = sin(t * 16.0 + phi * 3.0 - u_time * 24.0) * cos(phi * 2.0 - u_time * 8.0);
    
    // Trộn hai thành phần gồ ghề
    float irregularity = mix(swell, bump, 0.4);
    
    // Giảm dần độ gồ ghề ở hai đầu mút (cận gốc và cận ngọn) để mesh thuôn mượt
    float dampen = smoothstep(0.01, 0.12, t) * smoothstep(0.99, 0.88, t);
    
    // Biên độ đẩy ngọn lửa gồ ghề
    float displacement = irregularity * dampen * 4.5; 

    vec3 displacedPos = vertexPosition + vertexNormal * displacement;
    
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    
    gl_Position = mvp * vec4(displacedPos, 1.0);
}
