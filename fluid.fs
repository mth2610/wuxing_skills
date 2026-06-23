#version 330

in vec3 fragPosition; // [cite: 1]
in vec2 fragTexCoord; // [cite: 1]
in vec3 fragNormal; // [cite: 1]

uniform float u_timeFS; // [cite: 1]
uniform vec3 viewPos; // [cite: 1]

out vec4 finalColor; // [cite: 1]

void main() {
    vec3 normal = normalize(fragNormal); // [cite: 2]
    vec3 viewDir = normalize(viewPos - fragPosition); // [cite: 2]

    // Đặt mặt trời nhân tạo ngay trước Camera chếch góc 45 độ để vệt bóng sáng chói rực trực diện
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5)); // [cite: 3]

    // Biến điệu gợn viền của khối nước 3D (Normal Map ảo)
    float waveNoise = sin(fragPosition.y * 0.2 + u_timeFS * 6.0) * cos(fragPosition.z * 0.2 - u_timeFS * 5.0); // [cite: 4]
    normal = normalize(normal + vec3(waveNoise * 0.12)); // [cite: 5]

    // Hiệu ứng thấu kính trong suốt 3D (Fresnel Effect)
    float NdotV = max(dot(normal, viewDir), 0.0); // [cite: 5]
    float fresnel = pow(1.0 - NdotV, 3.0); // Tăng cường độ mỏng viền sáng [cite: 6]

    // Gam màu ngọc lam rực rỡ đặc quánh chất lỏng từ link ArtStation reference
    vec3 waterCore = vec3(0.08, 0.45, 0.88); // [cite: 6]
    vec3 waterEdge = vec3(0.55, 0.92, 1.00); // [cite: 7, 8]
    
    // Tạo màu nền cơ bản hòa trộn giữa lõi và viền sáng
    vec3 baseColor = mix(waterCore, waterEdge, fresnel); // [cite: 8]

    // =========================================================================
    // NÂNG CẤP ĐÁY 1: Giả lập ánh sáng hắt ngược từ nền đất lên (Ground Bounce / Fake SSS)
    // Giúp phần đáy quả cầu ngậm sáng xanh lơ, không bị lì một màu tối đen đơn điệu
    // =========================================================================
    float groundBounce = max(dot(normal, vec3(0.0, -1.0, 0.0)), 0.0);
    baseColor += waterEdge * groundBounce * 0.5;

    // Vệt sáng gương phản chiếu (Specular Highlight) - Linh hồn tạo khối 3D cho thạch lỏng
    vec3 halfVector = normalize(lightDir + viewDir); // [cite: 9]
    float NdotH = max(dot(normal, halfVector), 0.0); // [cite: 10]
    float specular = pow(NdotH, 128.0) * 4.2; // Độ gắt cực mạnh như gương bóng [cite: 11]

    // =========================================================================
    // NÂNG CẤP ĐÁY 2: Tính toán Caustics dựa trên Normal thay vì fragTexCoord 
    // Giải quyết triệt để lỗi thắt nút, co rúm vân nước tại cực đáy (UV Pole Pinching)
    // =========================================================================
    float caustics = sin(normal.x * 7.0 + u_timeFS * 5.0) * cos(normal.z * 7.0 - u_timeFS * 4.0); // [cite: 11]
    caustics = pow(max(caustics, 0.0), 2.5) * 0.7 * (1.0 - NdotV); // [cite: 12]

    vec3 finalRGB = baseColor + vec3(specular) + vec3(0.6, 0.9, 1.0) * caustics; // [cite: 13]

    // Làm dịu màng bóng tối viền cực biên (Rim Darken) để đường chốt cạnh đáy mượt mà hơn
    float rimDarken = smoothstep(0.85, 1.0, 1.0 - NdotV); // [cite: 14]
    finalRGB = mix(finalRGB, vec3(0.02, 0.10, 0.30), rimDarken * 0.2); // Giảm lực nhuộm tối từ 0.5 về 0.2 [cite: 15]

    // Cân bằng lại Alpha: Lòng quả cầu cực trong suốt (0.15) giúp nhìn xuyên bản đồ, 
    // nhưng rìa ngoài vẫn giữ độ căng (0.85) để bật rõ hình dáng khối cầu núng nính
    float alpha = mix(0.85, 0.15, NdotV); // [cite: 15]
    finalColor = vec4(finalRGB, alpha); // [cite: 16]
}