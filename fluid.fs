#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;

out vec4 finalColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float waveNoise = sin(fragPosition.y * 0.2 + u_time * 6.0) * cos(fragPosition.z * 0.2 - u_time * 5.0);
    normal = normalize(normal + vec3(waveNoise * 0.12));

    float NdotV = max(dot(normal, viewDir), 0.0);
float fresnel = pow(1.0 - NdotV, 5.0); // Tăng từ 3.0 lên 5.0 làm viền mỏng đi rõ rệt

    // Bảng màu: Xanh Biển (Ocean Blue)
    vec3 waterCore = vec3(0.05, 0.30, 0.75); // Lõi xanh biển sâu
    vec3 waterEdge = vec3(0.30, 0.80, 1.00); // Viền xanh sáng
    
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    // Vân sóng caustics màu xanh biển
    float caustics = sin(normal.x * 7.0 + u_time * 5.0) * cos(normal.z * 7.0 - u_time * 4.0);
    caustics = pow(max(caustics, 0.0), 3.0); 
    baseColor += vec3(0.3, 0.6, 0.9) * caustics * 0.3; 

    // Phản quang tinh tế
    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
float specular = pow(NdotH, 256.0) * 5.0; // Tăng từ 128.0 lên 256.0

    vec3 finalRGB = baseColor + vec3(specular);

    // Độ trong suốt cực cao (0.08)
    float alpha = mix(0.08, 0.75, fresnel); 

    finalColor = vec4(finalRGB, alpha);
}