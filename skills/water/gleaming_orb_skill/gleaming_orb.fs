#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"

// Chỉ khai báo uniform đặc thù của skill nếu có. Không tái khai báo biến hệ thống!

void main() {
    // 1. Tạo hiệu ứng gợn sóng nước di động (Núng nính) qua biến đổi Vector Pháp tuyến (Normal)
    // Sử dụng tỉ lệ tọa độ thế giới thấp (0.08) để chống hiện tượng nhiễu hạt TV-static (Rule 11.2)
    vec3 worldCoord = fragPosition * 0.08; 
    
    // Tạo sóng hình sin đa hướng đan xen theo thời gian thực u_time
    float waveX = sin(worldCoord.x * 5.0 + u_time * 2.5) * cos(worldCoord.z * 3.5 + u_time * 1.8);
    float waveY = cos(worldCoord.y * 4.0 - u_time * 2.0) * sin(worldCoord.x * 3.0 + u_time * 2.2);
    vec3 waveOffset = vec3(waveX * 0.12, waveY * 0.12, waveX * 0.05);
    
    // Hòa trộn độ nhiễu sóng vào normal gốc của khối mesh để tạo màng nước biến động
    vec3 normal = normalize(fragNormal + waveOffset);
    
    // 2. Thiết lập hướng quan sát và hướng luồng sáng môi trường (Đại diện cho Đêm Isometric Arena)
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.6, 1.0, 0.4)); // Luồng sáng chếch từ trên cao xuống
    
    // 3. Tính toán chiếu sáng cơ bản sử dụng thư viện Engine (Rule 10)
    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = calcSpecular(normal, lightDir, viewDir, 45.0); // Độ bóng cao cho chất lỏng
    float fresnel = calcFresnel(normal, viewDir, 2.5);              // Phản xạ màng nước ngoài
    
    // 4. Phối màu nước chuyển tầng (Multi-stage Volume Color)
    vec3 waterBase = vec3(0.16, 0.50, 0.72); // Sắc xanh Cyan-Blue gốc của hệ Thủy (Rule 2)
    vec3 waterDeep = vec3(0.08, 0.25, 0.45); // Sắc xanh thẫm khi khuất sáng
    vec3 gleamColor = vec3(0.75, 0.93, 1.0);  // Sắc trắng xanh óng ánh phản chiếu (Long lanh)
    
    // Nội suy màu dựa trên cường độ khuếch tán ánh sáng (Diffuse)
    vec3 diffuseComponent = mix(waterDeep, waterBase, diffuse);
    
    // Ép màng phản chiếu Fresnel lên viền ngoài quả cầu nước
    vec3 finalRGB = mix(diffuseComponent, gleamColor, fresnel * 0.65);
    
    // Cộng hưởng điểm sáng Specular cực đại để tạo độ long lanh như thủy tinh/nước bọc
    finalRGB += gleamColor * specular * 0.9;
    
    // 5. Thượng tôn Luật Thẩm Mỹ Chống Robot (Rule 12.5)
    // Giới hạn vùng tự phát sáng (Emissive) trong khoảng diện tích 20%-30% bằng hàm smoothstep
    float coreGleamMask = smoothstep(0.72, 0.90, dot(normal, viewDir));
    vec3 emissiveCore = gleamColor * coreGleamMask * 0.35;
    
    // Cộng vùng phát sáng vào SAU CÙNG để bảo toàn khối 3D và độ rực rỡ không bị cháy sáng
    finalRGB += emissiveCore;
    
    // Trả về kết quả màu cuối cùng với Alpha trong suốt đặc trưng của chất lỏng
    finalColor = vec4(finalRGB, 0.82);
}