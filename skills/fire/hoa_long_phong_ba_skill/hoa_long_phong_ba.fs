#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0; // Nhận texture nhiễu (noise.png)
uniform float u_time;
uniform vec3 viewPos;
uniform float u_uvLength;
uniform float u_dissolve; // 0.0 -> 1.0 để điều khiển tan rã ống lửa

out vec4 finalColor;

// Hàm tạo nhiễu ngẫu nhiên giả lập từ texture nhiễu cuộn
float getNoiseValue(vec2 uv) {
    // Cuộn 2 luồng texture nhiễu ngược hướng nhau
    vec2 uvA = uv * 2.0 + vec2(u_time * 0.4, -u_time * 1.5);
    vec2 uvB = uv * 4.0 + vec2(-u_time * 0.2, -u_time * 2.5);
    
    float nA = texture(texture0, uvA).r;
    float nB = texture(texture0, uvB).r;
    
    return nA * 0.6 + nB * 0.4;
}

// Tính toán gập ghềnh bề mặt để tính normal động
float getIrregularity(vec2 uv) {
    float t = uv.y / u_uvLength;
    float phi = uv.x * 6.28318;
    float swell = sin(t * 10.0 - u_time * 18.0) * 0.6 + cos(t * 5.0 - u_time * 10.0) * 0.4;
    float bump = sin(t * 16.0 + phi * 3.0 - u_time * 24.0) * cos(phi * 2.0 - u_time * 8.0);
    float irregularity = mix(swell, bump, 0.4);
    float dampen = smoothstep(0.01, 0.12, t) * smoothstep(0.99, 0.88, t);
    return irregularity * dampen * 4.5; 
}

void main() {
    // 1. Tính toán normal động bằng đạo hàm hữu hạn (Finite differences)
    float eps = 0.015; 
    float dL = getIrregularity(fragTexCoord - vec2(eps, 0.0));
    float dR = getIrregularity(fragTexCoord + vec2(eps, 0.0));
    float dD = getIrregularity(fragTexCoord - vec2(0.0, eps));
    float dU = getIrregularity(fragTexCoord + vec2(0.0, eps));
    
    vec3 dNormal = vec3(dL - dR, dD - dU, 0.0);
    vec3 tangent = cross(vec3(0.0, 1.0, 0.0), fragNormal);
    if (length(tangent) < 0.1) {
        tangent = cross(vec3(1.0, 0.0, 0.0), fragNormal);
    }
    tangent = normalize(tangent);
    vec3 bitangent = cross(fragNormal, tangent);
    vec3 normal = normalize(fragNormal + (tangent * dNormal.x + bitangent * dNormal.y) * 0.6);

    // 2. Tính toán hướng nhìn và Fresnel
    vec3 viewDir = normalize(viewPos - fragPosition);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.5);

    // 3. Đọc mẫu nhiễu để định hình luồng lửa cuộn
    float nVal = getNoiseValue(fragTexCoord);
    
    // Tỉ lệ đầu/đuôi vuốt thuôn gọn
    float t = fragTexCoord.y / u_uvLength;
    float fadeDamp = smoothstep(0.0, 0.15, t) * smoothstep(1.0, 0.82, t);

    // Tính toán độ tan rã (dissolve) kết hợp cùng độ mòn tự nhiên
    float activeErosion = 0.38; // Ngưỡng đục lỗ mặc định tạo lưỡi lửa rách rưới
    if (u_dissolve > 0.0) {
        activeErosion = mix(0.38, 1.0, u_dissolve);
    }

    // Đục lỗ tạo hình ngọn lửa rách rưới khí động học
    if (nVal * fadeDamp < activeErosion) {
        discard;
    }

    // 4. Phân phối màu sắc nhiệt lượng theo từng lớp (Tránh phủ trắng toàn bộ)
    float heat = nVal * fadeDamp * 1.25;

    vec3 darkFire  = vec3(0.28, 0.01, 0.00); // Lửa đỏ thẫm / tro bụi tối ở rìa lỗ
    vec3 deepRed   = vec3(0.72, 0.05, 0.01); // Lửa đỏ đậm cuốn hút
    vec3 hotOrange = vec3(1.00, 0.30, 0.00); // Cam sáng rực
    vec3 yellowCore= vec3(1.00, 0.88, 0.35); // Lõi vàng nóng
    vec3 whiteHot  = vec3(1.00, 0.99, 0.78); // Điểm trắng nóng đỉnh điểm

    vec3 fireColor = darkFire;
    if (heat > 0.38) {
        fireColor = mix(darkFire, deepRed, clamp((heat - 0.38) / 0.12, 0.0, 1.0));
    }
    if (heat > 0.50) {
        fireColor = mix(fireColor, hotOrange, clamp((heat - 0.50) / 0.18, 0.0, 1.0));
    }
    if (heat > 0.68) {
        fireColor = mix(fireColor, yellowCore, clamp((heat - 0.68) / 0.17, 0.0, 1.0));
    }
    if (heat > 0.85) {
        fireColor = mix(fireColor, whiteHot, clamp((heat - 0.85) / 0.15, 0.0, 1.0));
    }

    // 5. Shading 3D: Tính toán ánh sáng khuếch tán (Diffuse) giả lập để tạo chiều sâu khối ống
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.3));
    float diff = max(dot(normal, lightDir), 0.0);
    float shadow = mix(0.40, 1.0, diff); // Giảm sáng ở các góc khuất sáng
    fireColor *= shadow;

    // Tăng cường hào quang phát sáng ở mép ngoài bằng Fresnel
    fireColor += deepRed * fresnel * 1.4;

    // Hơi thở ngọn lửa dao động nhẹ theo thời gian
    float breathe = 1.25f + 0.12f * sin(u_time * 5.5);
    fireColor *= breathe;

    // Độ trong suốt mượt mà ở biên đục lỗ
    float alpha = smoothstep(activeErosion, activeErosion + 0.12, nVal * fadeDamp);
    alpha = mix(alpha, 1.0, fresnel * 0.55);
    alpha = clamp(alpha, 0.0, 1.0);
    
    if (u_dissolve > 0.0) {
        alpha *= (1.0 - u_dissolve);
    }

    finalColor = vec4(fireColor, alpha);
}
