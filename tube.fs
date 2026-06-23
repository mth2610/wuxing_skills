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

    // 2 luồng vân nước cuộn lệch nhau để phá vỡ sự đều đặn (Grid-like)
    vec2 flow1 = fragTexCoord - vec2(u_time * 1.5, u_time * 8.0);
    vec2 flow2 = vec2(fragTexCoord.x * 2.0, fragTexCoord.y * 0.7) - vec2(-u_time * 2.5, u_time * 12.0);

    // Nhiễu bề mặt hỗn loạn từ sự giao thoa 2 luồng chảy
    float waveNoise = sin(flow1.y * 20.0 + flow2.x * 15.0) * cos(flow2.y * 25.0);
    normal = normalize(normal + vec3(waveNoise * 0.45)); // Tăng mạnh độ méo pháp tuyến

    // Fresnel Effect (Thấu kính)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 2.5);

    // Xanh lục lam rực rỡ
    vec3 waterCore = vec3(0.00, 0.40, 0.85); 
    vec3 waterEdge = vec3(0.60, 0.95, 1.00); 
    vec3 baseColor = mix(waterCore, waterEdge, fresnel);

    float groundBounce = max(dot(normal, vec3(0.0, -1.0, 0.0)), 0.0);
    baseColor += waterEdge * groundBounce * 0.5;

    // Specular bị băm nhỏ bởi normal mới, tạo ra các vệt lấp lánh như kim tuyến
    vec3 halfVector = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVector), 0.0);
    float specular = pow(NdotH, 64.0) * 6.0; 

    // Caustics hỗn loạn: Không còn là lưới ô vuông
    float caustics = sin(flow1.x * 30.0) * cos(flow2.y * 20.0) + sin(flow2.x * 45.0);
    caustics = pow(max(caustics, 0.0), 2.0) * 1.8 * (1.0 - NdotV);

    vec3 finalRGB = baseColor + vec3(specular) + vec3(0.8, 0.95, 1.0) * caustics;

    // Lõi trong suốt, rìa sáng đậm
    float alphaFresnel = mix(0.95, 0.15, NdotV);
    float edgeFade = smoothstep(0.0, 0.15, fragTexCoord.y);
    
    finalColor = vec4(finalRGB, alphaFresnel * edgeFade);
}