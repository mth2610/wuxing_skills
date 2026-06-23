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

    // Đặt mặt trời nhân tạo ngay trước Camera chếch góc 45 độ để vệt bóng sáng chói rực trực diện
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    // Biến điệu gợn viền của khối nước 3D (Normal Map ảo)
    float waveNoise = sin(fragPosition.y * 0.2 + u_time * 6.0) * cos(fragPosition.z * 0.2 - u_time * 5.0);
    normal = normalize(normal + vec3(waveNoise * 0.12));

    // Hiệu ứng thấu kính trong suốt 3D (Fresnel Effect)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0); // Tăng cường độ mỏng viền sáng

    // Gam màu ngọc lam rực rỡ đặc quánh chất lỏng
    vec3 waterCore = vec3(0.08, 0.45, 0.88);
    vec3 waterEdge = vec3(0.55, 0.92, 1.00);
    
    // Tạo màu nền cơ bản hòa trộn giữa lõi và viền sáng
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    // =========================================================================
    // Giả lập ánh sáng hắt ngược từ nền đất lên (Ground Bounce / Fake SSS)
    // Giúp phần đáy quả cầu ngậm sáng xanh lơ, không bị lì một màu tối đen đơn điệu
    // =========================================================================
    float groundBounce = max(dot(normal, vec3(0.0, -1.0, 0.0)), 0.0);
    baseColor += waterEdge * groundBounce * 0.5;

    // Vệt sáng gương phản chiếu (Specular Highlight) - Linh hồn tạo khối 3D cho thạch lỏng
    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specular = pow(NdotH, 128.0) * 4.2; // Độ gắt cực mạnh như gương bóng

    // =========================================================================
    // Caustics dựa trên Normal thay vì fragTexCoord
    // Giải quyết lỗi thắt nút, co rúm vân nước tại cực đáy (UV Pole Pinching)
    // =========================================================================
    float caustics = sin(normal.x * 7.0 + u_time * 5.0) * cos(normal.z * 7.0 - u_time * 4.0);
    caustics = pow(max(caustics, 0.0), 2.5) * 0.7 * (1.0 - NdotV);

    vec3 finalRGB = baseColor + vec3(specular) + vec3(0.6, 0.9, 1.0) * caustics;

    // Làm dịu màng bóng tối viền cực biên (Rim Darken) để đường chốt cạnh đáy mượt mà hơn
    float rimDarken = smoothstep(0.85, 1.0, 1.0 - NdotV);
    finalRGB = mix(finalRGB, vec3(0.02, 0.10, 0.30), rimDarken * 0.2);

    // Cân bằng Alpha: lòng quả cầu cực trong suốt (0.15) nhìn xuyên bản đồ,
    // rìa ngoài giữ độ căng (0.85) để bật rõ hình dáng khối cầu núng nính
    float alpha = mix(0.85, 0.15, NdotV);
    finalColor = vec4(finalRGB, alpha);
}