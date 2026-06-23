#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform float u_time;
uniform vec3 viewPos;
uniform float u_uvLength;

out vec4 finalColor;

float getIrregularity(vec2 uv) {
    float t = uv.y / u_uvLength;
    float phi = uv.x * 6.28318;
    
    // Sóng dài, mượt, không xoắn
    float swell = sin(t * 4.0 - u_time * 12.0);
    float bump = sin(t * 8.0 - u_time * 20.0) * cos(phi * 2.0);
    
    float irregularity = swell * 0.5 + bump * 0.5;
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    
    return irregularity * dampen * 4.0; 
}

void main() {
    float eps = 0.02; 
    float dL = getIrregularity(fragTexCoord - vec2(eps, 0.0));
    float dR = getIrregularity(fragTexCoord + vec2(eps, 0.0));
    float dD = getIrregularity(fragTexCoord - vec2(0.0, eps));
    float dU = getIrregularity(fragTexCoord + vec2(eps, 0.0));
    
    vec3 dNormal = vec3(dL - dR, dD - dU, 0.0);
    vec3 tangent = normalize(cross(vec3(0.0, 1.0, 0.0), fragNormal));
    if (length(tangent) < 0.1) tangent = normalize(cross(vec3(1.0, 0.0, 0.0), fragNormal));
    vec3 bitangent = cross(fragNormal, tangent);
    vec3 normal = normalize(fragNormal + (tangent * dNormal.x + bitangent * dNormal.y) * 0.5);

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
    float NdotV = max(dot(normal, viewDir), 0.0);
float fresnel = pow(1.0 - NdotV, 5.0); // Tăng từ 3.0 lên 5.0 làm viền mỏng đi rõ rệt

    // Bảng màu: Xanh Biển Sâu (Deep Ocean Blue)
    vec3 waterCore = vec3(0.02, 0.30, 0.70); // Xanh biển đậm
    vec3 waterEdge = vec3(0.30, 0.70, 0.95); // Xanh biển sáng
    
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    // Vân nước chạy dọc dịu nhẹ
    vec2 scroll1 = fragTexCoord * vec2(1.0, 0.5) - vec2(0.0, u_time * 4.0);
    float caustics = sin(scroll1.x * 6.0) * cos(scroll1.y * 5.0);
    baseColor += vec3(0.2, 0.5, 0.9) * pow(max(caustics, 0.0), 2.0) * 0.2; 

    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
float specular = pow(NdotH, 256.0) * 5.0; // Tăng từ 128.0 lên 256.0

    vec3 finalRGB = baseColor + vec3(specular);
    float alpha = mix(0.3, 0.9, fresnel);

    finalColor = vec4(finalRGB, alpha);
}