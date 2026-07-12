#version 330
#include "core/shaders/common/vs_header.glsl"

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)
uniform float u_customParam2; // Random phase offset

void main() {
    vec3 animatedPos = vertexPosition;
    
    // ==========================================================
    // GIAI ĐOẠN 1 & 2: DÂNG LÊN & BUNG XÒE (Tạo hiệu ứng "Nở Hoa")
    // ==========================================================
    
    // Nhịp 1: Bắn vọt lên theo trục Y rất nhanh (0.0 -> 0.15)
    float riseProgress = smoothstep(0.0, 0.15, u_customParam1);
    
    // Nhịp 2: Bung xòe trục XZ (0.10 -> 0.25). 
    // Chú ý: Bắt đầu trễ hơn một chút (0.10) so với Rise để tạo thành cột nước trước.
    float spreadProgress = smoothstep(0.10, 0.25, u_customParam1);
    
    // Bóp méo XZ: Khi spreadProgress = 0, ép mesh dẹp lại thành cột nước hẹp (0.15)
    // Khi spreadProgress = 1, trả mesh về nguyên bản bung xòe hết cỡ (1.0)
    float currentSpread = mix(0.15, 1.0, spreadProgress); 

    // Áp dụng biến dạng: Cột nước dâng lên trước, xòe ra sau
    animatedPos.y *= riseProgress;
    animatedPos.x *= currentSpread;
    animatedPos.z *= currentSpread;
    
    // ==========================================================
    // GIAI ĐOẠN 3: RƠI XUỐNG BỞI TRỌNG LỰC (GRAVITY)
    // ==========================================================
    // Khi t > 0.45, trọng lực kéo nước xuống nhanh dần đều
    float fallProgress = smoothstep(0.45, 1.0, u_customParam1);
    animatedPos.y -= (fallProgress * fallProgress) * 2.5; 
    
    // Khi rơi xuống đập mặt đất, nước lả ra thêm một chút
    float splat = 1.0 + (fallProgress * 0.3);
    animatedPos.x *= splat;
    animatedPos.z *= splat;

    // ==========================================================
    // RUNG ĐỘNG BỀ MẶT (WOBBLE)
    // ==========================================================
    vec4 worldPos = matModel * vec4(animatedPos, 1.0);
    vec3 validNormal = vertexNormal;
    if (length(validNormal) < 0.1) {
        validNormal = normalize(animatedPos); 
    }
    
    // Nước luôn rung rinh nhẹ khi chuyển động
    float wobble = sin(worldPos.y * 6.0 + worldPos.x * 4.0 - (u_time + u_customParam2) * 12.0);
    // Chỉ rung phần ngọn, phần gốc bám đất ít rung hơn
    float heightFactor = clamp(animatedPos.y, 0.0, 1.0);
    float displacementAmt = wobble * 0.06 * heightFactor * riseProgress; 
    
    vec3 displacedPos = animatedPos + validNormal * displacementAmt;
    
    VS_FinalOutput(displacedPos);
}