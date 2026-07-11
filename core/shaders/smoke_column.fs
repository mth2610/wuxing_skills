#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

// Highly optimized rising-column smoke density field.
// Uses 2D value noise and 2D FBM with domain warping and seed generation based
// on world-space fragment coordinates. Minimizes GPU fill rate footprint.

uniform vec4  u_color;
uniform float u_progress;   // looped birth->climb->dissolve ramp, 0..1
uniform float u_diffusion;  // lateral spread rate
uniform float u_noiseScale; // turbulence warp domain frequency
uniform float u_driftSpeed; // turbulence warp animation speed
uniform float u_riseSpeed;  // vertical advection speed
uniform float u_seed;       // optional seed offset

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; 
    float h = clamp((uv.y + 1.0) * 0.5, 0.0, 1.0); 

    // ==========================================
    // 1. KHỐNG CHẾ TỐC ĐỘ TUYỆT ĐỐI
    // ==========================================
    float timeRise = u_time * (u_riseSpeed * 0.01 + 0.05); 
    float timeDrift = u_time * (u_driftSpeed * 0.02 + 0.1);

    // Giải mã seed từ trục Y của normal vector nhận từ vertex (tránh phân mảnh pixel và loại bỏ flushes)
    float lenXZ = length(fragNormal.xz);
    float seedVal = u_seed + ((lenXZ > 0.0001) ? (fragNormal.y / lenXZ) * 100.0 : 0.0);
    vec2 seedOff = vec2(seedVal * 13.13, seedVal * 27.37);

    // ==========================================
    // 2. TĂNG MẠNH ĐỘ UỐN LƯỢN VÀ NHIỄU (Macro Curl & Domain Warp)
    // ==========================================
    // Sử dụng 2D noise (vnoise) cực nhẹ thay vì 3D noise (tiết kiệm 50% số lần hash)
    vec2 dom0 = vec2(uv.x * 1.8 + timeDrift * 0.3, (uv.y - timeRise) * 1.8) + seedOff;
    vec2 n0 = vec2(vnoise(dom0), vnoise(dom0 + vec2(7.2, 3.4))) - 0.5;

    vec2 dom1 = (vec2(uv.x, uv.y - timeRise * 1.3) + n0 * 0.6) * 3.5 + seedOff;
    vec2 n1 = vec2(vnoise(dom1), vnoise(dom1 + vec2(5.2, 1.3))) - 0.5;

    // Vùng chuyển pha: Khói bắt đầu lượn sóng từ độ cao 1% trở lên
    float curlZone = smoothstep(0.01, 0.8, h); 

    // LẮC LƯ (Sway): Zic-zac uốn lượn
    float sway = sin(h * 4.5 - timeRise * 3.5 + seedVal * 0.01) * 0.35 * curlZone;

    // Bẻ cong cả hai chiều trục tọa độ (Domain Warping)
    float warpStrength = mix(0.0, 1.4, curlZone); 
    vec2 warpedUV = uv + (n0 * 0.85 + n1 * 0.15) * warpStrength;
    
    // Sử dụng warpedUV để tính toán khoảng cách ngang dx
    float dx = warpedUV.x - sway;
    float r2 = dx * dx; 

    // warpedH cho phép viền và độ tan biến uốn lượn theo chiều dọc
    float warpedH = clamp((warpedUV.y + 1.0) * 0.5, 0.0, 1.0);

    // ==========================================
    // 3. KHUẾCH TÁN & CUỘN KHÓI (Billows Modulation)
    // ==========================================
    float D = max(u_diffusion * 0.08, 0.005);
    float sigma0Sq = 0.004; 

    // 2D FBM (fbm2) trôi ngang cuộn dọc rất nhẹ
    vec2 billowCoords = vec2(warpedUV.x * 2.8 + timeDrift * 0.4, (warpedUV.y - timeRise) * 2.8) + seedOff;
    float billowNoise = fbm2(billowCoords);
    
    // Điều chỉnh độ phình (swelling) của cột khói theo tiếng ồn cuộn
    float puffMod = 0.6 + 1.4 * billowNoise * curlZone;
    float sigmaSq = (sigma0Sq + 2.0 * D * warpedH) * puffMod;

    // Bảo toàn khối lượng khói
    float ampDiffusion = sqrt(sigma0Sq / sigmaSq);

    // ==========================================
    // 4. MẬT ĐỘ & CUỘN TRÒN (Billow Density Modulation)
    // ==========================================
    float intensity = 2.2;
    float ampTopFade = 1.0 - pow(warpedH, 3.0); 

    // Nhân thêm mật độ cuộn để tạo ra các cục/khối khói tròn (billows) rõ rệt
    float billowDensityMod = 0.3 + 1.5 * billowNoise;
    float baseDensity = ampDiffusion * ampTopFade * exp(-r2 / (2.0 * sigmaSq)) * intensity * billowDensityMod;
    baseDensity = 1.0 - exp(-baseDensity * 2.0);

    // Bào mòn viền mềm mại ở nửa trên bằng nhiễu chi tiết (2D FBM)
    vec2 edgeCoords = vec2(uv.x * 4.5 - timeDrift * 0.2, (uv.y - timeRise * 1.5) * 4.5) + seedOff;
    float edgeNoise = fbm2(edgeCoords);
    float erosion = edgeNoise * pow(warpedH, 1.8) * 0.75;
    float density = max(baseDensity - erosion, 0.0);

    float alpha = clamp(density, 0.0, 1.0);

    // ==========================================
    // 5. HIỂN THỊ VÀ BO BIÊN
    // ==========================================
    float headY = u_progress * 1.0; 
    float propagateMask = smoothstep(headY, headY - 0.3, warpedH);
    alpha *= propagateMask;

    // Fade viền ngang rộng rãi để tránh lộ mép
    alpha *= 1.0 - smoothstep(0.15, 1.0, abs(warpedUV.x));
    alpha *= 1.0 - smoothstep(0.97, 1.0, warpedH);

    finalColor = vec4(u_color.rgb, alpha * u_color.a);
}