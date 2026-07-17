#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

// ĐÃ XÓA u_center. Không cần C code truyền toạ độ nữa!
uniform float u_radius;
uniform float u_progress;

void main() {
    // 1. Lấy hướng tia nhìn từ Camera đi xuyên qua mặt khối cầu
    vec3 rayDir = normalize(fragPosition - viewPos);
    
    // Đảm bảo bán kính hợp lệ
    float safeRadius = max(u_radius, 0.5);
    
    // 2. TÁI TẠO LOCAL SPACE SIÊU VIỆT:
    // Vì DrawCoreSphere vẽ mặt ngoài, fragNormal luôn hướng từ tâm ra vỏ.
    // Lấy Pháp tuyến nhân với Bán kính -> Ta có chính xác toạ độ 3D cục bộ (Local Space) 
    // của điểm hiện tại so với tâm, mà không cần biết tâm ở đâu!
    vec3 p = normalize(fragNormal) * safeRadius;
    
    // Đẩy nhẹ tia ray vào trong lòng khối cầu để tránh kẹt ở vỏ ngoài
    p += rayDir * 0.05; 
    
    float stepSize = safeRadius * 0.1; 
    vec4 sum = vec4(0.0);
    
    // 3. Volumetric Raymarching (Từ trước ra sau)
    for (int i = 0; i < 20; i++) {
        float distToCenter = length(p);
        
        // Nếu tia ray đâm xuyên qua mặt sau của khối cầu -> Dừng lại
        if (distToCenter > safeRadius) {
            break;
        }
        
        // Tính độ mờ dần từ tâm ra vỏ (Mũ 2 để tạo lõi đặc, viền mỏng)
        float mask = clamp(1.0 - (distToCenter / safeRadius), 0.0, 1.0);
        mask *= mask;
        
        // Dùng fbm2 chuẩn của Engine (Cực kỳ an toàn trên Vulkan/Mali) chiếu lên 2 mặt phẳng
        float n1 = fbm2(p.xz * 2.0 + vec2(u_time * 0.4, 0.0));
        float n2 = fbm2(p.xy * 2.0 - vec2(0.0, u_time * 0.3));
        
        // Khuếch đại nhiễu
        float noiseVal = n1 * n2 * 3.0;
        
        // Mật độ tại điểm hiện tại = Nhiễu x Mask
        float den = noiseVal * mask;
        
        // Nếu điểm này có khói, ta tiến hành tích luỹ màu
        if (den > 0.05) {
            // Khói dày ở lõi (den cao) -> Xám tối. Khói mỏng ở viền -> Xám sáng
            vec3 col = mix(vec3(0.85, 0.85, 0.9), vec3(0.35, 0.35, 0.4), clamp(den, 0.0, 1.0));
            
            // Tính Alpha cho step hiện tại
            float alpha = clamp(den * stepSize * 4.0, 0.0, 1.0);
            
            // Tích luỹ Alpha Blend có che khuất (Khói phía trước che khói phía sau)
            sum.rgb += col * alpha * (1.0 - sum.a);
            sum.a += alpha * (1.0 - sum.a);
        }
        
        // Tối ưu GPU: Nếu khói đã đặc xịt 100% rồi thì không cần tính tiếp mặt sau nữa
        if (sum.a > 0.95) break;
        
        // Tiến tia ray đi sâu vào khối cầu
        p += rayDir * stepSize;
    }

    // 4. Khói tan biến dần theo thời gian
    sum *= clamp(1.0 - u_progress, 0.0, 1.0);
    
    // Cắt bỏ các rìa dư thừa
    if (sum.a < 0.02) {
        discard;
    }
    
    // 5. GIẢI QUYẾT BẪY BLEND ALPHA
    // Ta phải đảo ngược phép nhân màu để cơ chế BLEND_ALPHA của Raylib hoà trộn chính xác
    if (sum.a > 0.0) {
        sum.rgb /= sum.a;
    }
    
    finalColor = sum;
}