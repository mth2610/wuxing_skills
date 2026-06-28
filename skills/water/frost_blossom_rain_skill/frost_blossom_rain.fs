#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;
uniform float u_dissolve;

out vec4 finalColor;

void main() {
    if (u_dissolve > 0.0) {
        // Hiệu ứng hòa tan (dissolve) tuyết bay
        float noise = sin(fragPosition.x * 25.0) * cos(fragPosition.z * 25.0);
        if (noise * 0.5 + 0.5 < u_dissolve) discard;
    }

    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.4));

    float NdotV = max(dot(normal, viewDir), 0.0);
    // Fresnel cực mạnh ở viền tạo viền tuyết trắng lấp lánh đặc trưng
    float fresnel = pow(1.0 - NdotV, 3.5);

    // Bảng màu pha lê tuyết băng
    vec3 baseColor = vec3(0.60, 0.85, 1.0);  // Xanh lam tuyết pha lê
    vec3 edgeColor = vec3(1.0, 1.0, 1.0);     // Trắng tuyết sáng rực ở viền

    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = baseColor * (NdotL * 0.65 + 0.35);

    // Specular phản chiếu cực bén
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specular = pow(NdotH, 64.0) * 1.5;

    vec3 finalRGB = mix(diffuse, edgeColor, fresnel * 0.85) + vec3(specular) * vec3(0.8, 0.95, 1.0);

    // Hiệu ứng lấp lánh (sparkle) ngẫu nhiên trên tinh thể băng
    float sparkle = sin(u_time * 8.0 + fragPosition.x * 60.0) * cos(u_time * 7.0 - fragPosition.z * 60.0);
    if (sparkle > 0.93) {
        finalRGB += edgeColor * 0.5;
    }

    // Alpha bán trong suốt tạo độ trong pha lê
    float alpha = mix(0.70, 0.95, fresnel);

    finalColor = vec4(finalRGB, alpha);
}
