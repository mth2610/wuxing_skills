#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    // Làm chậm tốc độ cuộn sóng hình thể
    float flowSpeed = u_time * 12.0;
    
    // Giảm mạnh biên độ (0.35 -> 0.15) để lấy lại form "núng nính" nguyên khối
    float wave = sin(vertexTexCoord.y * 5.0 - flowSpeed) * 0.15       
               + cos(vertexTexCoord.x * 6.0 + u_time * 6.0) * 0.10  
               + sin((vertexTexCoord.x * 2.0 + vertexTexCoord.y) * 15.0 - u_time * 8.0) * 0.05; 
               
    vec3 displacedPos = vertexPosition + vertexNormal * wave;
    
    fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;

    gl_Position = mvp * vec4(displacedPos, 1.0);
}