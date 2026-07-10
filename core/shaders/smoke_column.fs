#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

// Dedicated rising-column smoke density field — split off from
// energy_smoke.fs (2026-07-10). That shader's "shockwave ring" (a hollow
// ring expanding from a STATIC point) and a rising column (a blob that
// travels upward from a fixed base) are different physical shapes; trying
// to cover both from one shader via uniforms was hard to tune without one
// case regressing the other. This file only ever does one thing: a soft
// blob that starts at the bottom edge, climbs, widens, and fades — no ring/
// hollow-center logic at all.

uniform vec4  u_color;
uniform float u_progress;   // looped birth->climb->dissolve ramp, 0..1 (caller loops it — a
                            // persistent column has no natural "death" moment)
uniform float u_diffusion;  // lateral spread rate
uniform float u_noiseScale; // turbulence warp domain frequency
uniform float u_driftSpeed; // turbulence warp animation speed
uniform float u_riseSpeed;  // how far up the quad (in uv units, quad spans [-1,1]) the
                            // blob's centerline travels over one full progress loop
uniform float u_seed;       // per-instance random offset (caller randomizes e.g. 0..1000 at
                            // spawn time) — without this, if u_time resets per-cast the noise
                            // trajectory is fully deterministic and every cast looks identical

float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i);
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 4; k++) {
        v += a * vnoise3(p);
        p  = p * 2.15 + vec3(9.1, 3.7, 6.3);
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; 
    float h = clamp((uv.y + 1.0) * 0.5, 0.0, 1.0); 

    // ==========================================
    // 1. KHỐNG CHẾ TỐC ĐỘ TUYỆT ĐỐI
    // ==========================================
    // Ép các thông số thời gian phải chạy cực kỳ chậm, bất chấp biến u_riseSpeed to thế nào.
    float timeRise = u_time * (u_riseSpeed * 0.01 + 0.05); 
    float timeDrift = u_time * (u_driftSpeed * 0.02 + 0.1);

    vec2 scrolledUV = uv;
    scrolledUV.y -= timeRise;

    // ==========================================
    // 2. TĂNG MẠNH ĐỘ UỐN LƯỢN VÀ NHIỄU (Macro Curl)
    // ==========================================
    // Dùng hệ số cố định cho không gian thay vì u_noiseScale để đảm bảo vân khói luôn to, rõ ràng
    // Lệch pha ngẫu nhiên theo từng lần xuất chiêu — cùng công thức noise nhưng khác gốc tọa độ
    // nên hình dạng không lặp lại y hệt giữa các lần cast.
    vec3 seedOff = vec3(u_seed * 13.13, u_seed * 71.71, u_seed * 27.37);

    vec3 dom0 = vec3(scrolledUV * 1.5, timeDrift * 0.5) + seedOff;
    vec2 n0 = vec2(fbm3(dom0), fbm3(dom0 + vec3(7.2, 1.1, 3.4))) - 0.5;

    vec3 dom1 = vec3((scrolledUV + n0 * 0.5) * 3.0, timeDrift * 0.8) + seedOff;
    vec2 n1 = vec2(fbm3(dom1), fbm3(dom1 + vec3(5.2, 1.3, 0.0))) - 0.5;

    // Vùng chuyển pha: Khói bắt đầu lượn sóng từ độ cao 15% trở lên
    float curlZone = smoothstep(0.15, 0.8, h); 

    // LẮC LƯ (Sway): Tăng biên độ từ 0.06 lên 0.35. Bạn sẽ thấy đường zic-zắc rất rõ!
    // Cộng thêm pha lệch theo seed để hướng lắc cũng khác nhau giữa các lần cast.
    float sway = sin(h * 4.5 - timeRise * 3.0 + u_seed * 6.283185) * 0.35 * curlZone;

    // Bẻ cong và xé rách khói
    float warpStrength = mix(0.0, 1.2, curlZone); 
    float dx = uv.x - sway + (n0.x * 0.8 + n1.x * 0.2) * warpStrength;
    float r2 = dx * dx; 

    // ==========================================
    // 3. KHUẾCH TÁN (theo đúng nghiệm phương trình khuếch tán)
    // ==========================================
    // Nghiệm Gauss của pt khuếch tán 1D: C(x,t) ~ (1/sqrt(t)) * exp(-x^2 / (4*D*t)),
    // với sigma^2(t) = 2*D*t. Ở đây h (độ cao, 0..1) đóng vai trò "thời gian trôi qua"
    // kể từ lúc phần tử khói đó rời đáy cột — cột khói dâng lên đều nên h ~ t.
    float D = max(u_diffusion * 0.08, 0.005);
    float sigma0Sq = 0.004; // bề rộng gốc tại đáy (h=0), tránh sợi khói sắc lẹm như dây thép

    // Nếp cuộn turbulence làm nhiễu loạn cục bộ độ khuếch tán (rối loạn ~ khuếch tán rối)
    float puffMod = 1.0 + n0.y * 1.5 * curlZone;
    float sigmaSq = (sigma0Sq + 2.0 * D * h) * puffMod;

    // Biên độ đỉnh giảm theo 1/sqrt(t) để BẢO TOÀN khối lượng khói khi nó loang rộng ra
    // — đây là phần trước đây thiếu, khiến khói không mỏng dần tự nhiên mà chỉ bị erosion cắt cứng.
    float ampDiffusion = sqrt(sigma0Sq / sigmaSq);

    // ==========================================
    // 4. MẬT ĐỘ & TAN BIẾN Ở ĐỈNH
    // ==========================================
    float intensity = 2.0;
    float ampTopFade = 1.0 - pow(h, 3.0); // khói giữ trắng đục lâu hơn, sát đỉnh mới tan hẳn

    float baseDensity = ampDiffusion * ampTopFade * exp(-r2 / (2.0 * sigmaSq)) * intensity;
    baseDensity = 1.0 - exp(-baseDensity * 2.0);

    // Bào mòn viền mềm mại ở nửa trên (turbulence rối loạn bề mặt, không phải khuếch tán "sạch")
    float edgeNoise = fbm3(vec3(scrolledUV * 3.0, timeDrift) + seedOff);
    float erosion = edgeNoise * pow(h, 2.0) * 0.8;
    float density = max(baseDensity - erosion, 0.0);

    float alpha = clamp(density, 0.0, 1.0);

    // ==========================================
    // 5. HIỂN THỊ VÀ BO BIÊN
    // ==========================================
    // Nếu u_progress chạy quá nhanh, phần đầu khói sẽ giật. Ta giảm hệ số nhân xuống 1.0.
    float headY = u_progress * 1.0; 
    float propagateMask = smoothstep(headY, headY - 0.3, h);
    alpha *= propagateMask;

    // Fade viền chiều ngang rộng hơn để khói có chỗ uốn lượn mà không bị cắt phẳng mép
    alpha *= 1.0 - smoothstep(0.1, 1.0, abs(uv.x));
    // Dải an toàn RẤT mỏng chỉ để tránh cắt phẳng ngay mép texture (h=1.0).
    // Trước đây dải này rộng 0.85→1.0 và CHỒNG lên ampTopFade (vốn đã tự fade từ ~0.6 trở lên),
    // khiến vùng 0.8-1.0 gần như luôn mờ sẵn bất kể headY đang ở đâu — nhìn như khói "đứng lại"
    // dù u_progress vẫn đang chạy đều lên đỉnh. Thu hẹp còn 0.97→1.0 để tốc độ lan cảm nhận đều hơn.
    alpha *= 1.0 - smoothstep(0.97, 1.0, h);

    finalColor = vec4(u_color.rgb, alpha * u_color.a);
}