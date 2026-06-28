#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;

out vec4 finalColor; // [cite: 2]

// Hàm tạo độ lồi lõm chuyển động sinh động hơn
float getIrregularity(vec2 uv) {
    // Sóng chạy cuộn dọc theo quả cầu dựa trên thời gian
    float swell = sin(uv.y * 6.0 - u_time * 8.0) * cos(uv.x * 4.0 + u_time * 4.0); // [cite: 3, 4]
    float bump = sin(uv.x * 12.0 + u_time * 12.0) * sin(uv.y * 10.0 - u_time * 16.0);
    
    return (swell * 0.6 + bump * 0.4); // [cite: 5]
}

void main() {
    float eps = 0.01;
    
    // Tính toán lại sai số đạo hàm (Đã sửa lỗi dU ở trục Y) [cite: 7, 8]
    float dL = getIrregularity(fragTexCoord - vec2(eps, 0.0));
    float dR = getIrregularity(fragTexCoord + vec2(eps, 0.0));
    float dD = getIrregularity(fragTexCoord - vec2(0.0, eps));
    float dU = getIrregularity(fragTexCoord + vec2(0.0, eps)); // <-- Đã sửa thành trục Y thành công
    
    vec3 dNormal = vec3(dL - dR, dD - dU, 0.0); // [cite: 9]
    
    // Tạo hệ tọa độ cục bộ (TBN) để áp độ méo vào Normal gốc của khối cầu
    vec3 tangent = cross(vec3(0.0, 1.0, 0.0), fragNormal);
    if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), fragNormal); // [cite: 10]
    tangent = normalize(tangent);
    vec3 bitangent = cross(fragNormal, tangent);
    
    // Lực biến dạng Normal (Tăng hệ số từ 0.5 lên 1.2 để quả cầu méo dẻo rõ rệt hơn)
    vec3 normal = normalize(fragNormal + (tangent * dNormal.x + bitangent * dNormal.y) * 1.2); // [cite: 11]

    // --- Giữ nguyên phần tính toán ánh sáng và màu sắc mượt mà của bạn ---
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5)); // [cite: 12]
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0); // [cite: 13]

    float upFactor = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0); // [cite: 14]
    vec3 topColor = vec3(0.75, 0.92, 1.00); // [cite: 15]
    vec3 midColor = vec3(0.18, 0.55, 0.95); // [cite: 16]
    vec3 bottomColor = vec3(0.01, 0.08, 0.28); // [cite: 17]
    
    vec3 baseColor = mix(bottomColor, midColor, pow(upFactor, 1.2)); // [cite: 18]
    baseColor = mix(baseColor, topColor, pow(upFactor, 4.0)); // [cite: 19]

    // Vân nước chạy dọc sinh động
    vec2 scroll1 = fragTexCoord * vec2(1.0, 0.5) - vec2(0.0, u_time * 3.0); // [cite: 20]
    float caustics = sin(scroll1.x * 8.0) * cos(scroll1.y * 6.0); // [cite: 21]
    baseColor += vec3(0.2, 0.5, 0.9) * pow(max(caustics, 0.0), 2.0) * 0.4; // [cite: 22]

    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    baseColor += vec3(0.4, 0.8, 1.0) * fresnel * 0.15; // [cite: 23]
    float specular = pow(NdotH, 64.0) * 4.0; // Giảm bớt chút Specular để bóng bẩy tự nhiên [cite: 24]

    vec3 finalRGB = baseColor + vec3(specular);
    float alpha = mix(0.4, 0.85, fresnel);
    finalColor = vec4(finalRGB, alpha); // [cite: 25]
}