#version 330 core

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;

// Uniforms
uniform sampler2D texture0; // Texture vỏ cây (bark_crack.png)
uniform vec4 colDiffuse;

// Các Uniforms đặc quyền do Wuxing's SkillManager tự động bind
uniform vec3 u_viewPos;     // Vị trí camera để tính Fresnel
uniform float u_time;       // Thời gian thực để animate
uniform float u_dissolve;   // Từ 0.0 (nguyên vẹn) đến 1.0 (tan biến hoàn toàn)

out vec4 finalColor;

// Hàm tạo nhiễu ngẫu nhiên cho viền cháy khi tan biến
float random(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
}

void main()
{
    // 1. Lấy màu cơ bản từ Texture vỏ cây
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // 2. Setup Vector chiếu sáng (Quy tắc 9.3)
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3)); // Giả lập ánh trăng chiếu nghiêng
    
    // 3. Tính toán Lambertian Diffuse (Đổ bóng khối 3D)
    float NdotL = max(dot(normal, lightDir), 0.15); // Ambient sáng 15%
    vec3 diffuseLight = texColor.rgb * fragColor.rgb * NdotL;
    
    // 4. Tính toán Rim Light / Fresnel (Giữ thể tích 3D - Quy tắc 9.4)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float rimWeight = pow(1.0 - NdotV, 3.0);
    vec3 rimLight = vec3(0.18, 0.8, 0.44) * rimWeight * 0.5; // Ánh sáng lướt viền màu lục
    
    // 5. Sparse Emissive Mask - Nhựa cây phát sáng lấp lánh (Quy tắc 9.2 & 9.4)
    // Dùng hệ số tọa độ cực nhỏ (0.05) để tránh lỗi TV static
    float noiseVal = sin(fragPosition.y * 0.8 - u_time * 2.5) * cos(fragPosition.x * 0.5);
    noiseVal = (noiseVal + 1.0) * 0.5; // Đưa về dải 0.0 -> 1.0
    
    // Chỉ sáng lên ở những vùng vân nứt (giả định noise > 0.85)
    float glowMask = smoothstep(0.85, 0.95, noiseVal);
    vec3 emissive = vec3(0.18, 0.8, 0.44) * glowMask * 2.5; // Màu lục bảo phát sáng
    
    // Gom ánh sáng (Luôn cộng Emissive vào cuối cùng)
    vec3 rgb = diffuseLight + rimLight + emissive;
    
    // 6. Xử lý hiệu ứng tan biến (Dissolve)
    float noiseDissolve = random(floor(fragPosition * 10.0)); // Phân khối rác
    
    if (u_dissolve > 0.0) {
        if (noiseDissolve < u_dissolve) {
            discard; // Xóa pixel nếu nằm trong vùng tan biến
        }
        // Viền xanh sáng rực ngay mép bị tan biến
        if (noiseDissolve < u_dissolve + 0.08) {
            rgb = mix(rgb, vec3(0.5, 1.0, 0.5), 0.8);
        }
    }
    
    finalColor = vec4(rgb, texColor.a * fragColor.a);
}