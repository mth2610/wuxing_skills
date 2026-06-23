#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_timeFS;
uniform vec3 viewPos;

out vec4 finalColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    
    // Đặt mặt trời nhân tạo ngay trước Camera chếch góc 45 độ để vệt bóng sáng chói rực trực diện
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5)); 

    // Biến điệu gợn viền của khối nước 3D (Normal Map ảo)
    float waveNoise = sin(fragPosition.y * 0.2 + u_timeFS * 6.0) * cos(fragPosition.z * 0.2 - u_timeFS * 5.0);
    normal = normalize(normal + vec3(waveNoise * 0.12));

    // Hiệu ứng thấu kính trong suốt 3D (Fresnel Effect)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0); // Tăng cường độ mỏng viền sáng

    // Gam màu ngọc lam rực rỡ đặc quánh chất lỏng từ link ArtStation reference
    vec3 waterCore = vec3(0.08, 0.45, 0.88);  // Lõi nước sâu thẩm thấu
    vec3 waterEdge = vec3(0.55, 0.92, 1.00);  // Rìa nước khúc xạ xanh lơ phát rực
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    // Vệt sáng gương phản chiếu (Specular Highlight) - Linh hồn tạo khối 3D cho thạch lỏng
    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specular = pow(NdotH, 128.0) * 4.2; // Độ gắt cực mạnh như gương bóng

    // Vân nước khúc xạ chuyển động nội sinh cuộn xoáy
    float caustics = sin(fragTexCoord.x * 20.0 + u_timeFS * 5.0) * cos(fragTexCoord.y * 20.0 - u_timeFS * 4.0);
    caustics = pow(max(caustics, 0.0), 2.5) * 0.7 * (1.0 - NdotV);

    vec3 finalRGB = baseColor + vec3(specular) + vec3(0.6, 0.9, 1.0) * caustics;

    // Tạo màng bóng tối sẫm màu ở đường biên cực viền để bật khối tròn lồi hẳn ra khỏi map nền
    float rimDarken = smoothstep(0.82, 1.0, 1.0 - NdotV);
    finalRGB = mix(finalRGB, vec3(0.02, 0.12, 0.35), rimDarken * 0.5);

    // Độ alpha trong vắt ở tâm giúp nhìn xuyên thấu địa hình, rìa đục căng rực
    float alpha = mix(0.92, 0.42, NdotV);

    finalColor = vec4(finalRGB, alpha);
}