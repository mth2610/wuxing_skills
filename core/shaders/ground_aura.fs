#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_scrollSpeed;
uniform float u_noiseScale;

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
    for (int k = 0; k < 3; k++) {
        v += a * vnoise3(p);
        p  = p * 2.1 + vec3(7.3, 13.1, 5.7);
        a *= 0.5;
    }
    return v;
}

void main() {
    // 1. Chuyển đổi UV (0.0 -> 1.0) thành hệ tọa độ có tâm ở (0, 0)
    vec2 centeredUV = fragTexCoord - vec2(0.5);
    
    // Tính bán kính (r) từ tâm. Nhân 2 để mép viền chạm ngưỡng 1.0
    float r = length(centeredUV) * 2.0; 
    
    // Tính góc quay (Angle)
    float angle = atan(centeredUV.y, centeredUV.x);

    // ---------------------------------------------------------
    // 2. TẠO MẶT NẠ LÀM MỜ BIÊN (Radial Mask)
    // - Viền ngoài: Bắt đầu mờ từ bán kính 0.6 và biến mất hoàn toàn ở 1.0
    // - Tâm trong: Bắt đầu mờ từ 0.3 và trong suốt hoàn toàn ở tâm 0.0
    // ---------------------------------------------------------
    float edgeFade = smoothstep(1.0, 0.6, r);
    float centerFade = smoothstep(0.0, 0.3, r);
    float mask = edgeFade * centerFade;

    // ---------------------------------------------------------
    // 3. TẠO LUỒNG NĂNG LƯỢNG TỎA RA (Radiating Energy Wisps)
    // Bằng cách dùng góc cos/sin cho trục XY và bán kính r cho trục Z,
    // ta ép khối FBM trôi từ tâm ra ngoài liên tục mà không bị đứt gãy.
    // ---------------------------------------------------------
    vec3 dom = vec3(
        cos(angle) * u_noiseScale, 
        sin(angle) * u_noiseScale, 
        -r * (u_noiseScale * 0.5) - (u_time * u_scrollSpeed) // Trừ time để chảy ra ngoài
    );
    
    float n1 = fbm3(dom);
    float n2 = fbm3(dom * 1.8 + vec3(u_time * 0.4));
    
    // Ép các dải nhiễu thành dạng sợi sáng chói
    float ridge = 1.0 - abs(2.0 * n1 - 1.0);
    float wisp = ridge * (0.3 + 0.7 * n2);
    wisp = smoothstep(0.3, 0.8, wisp); // Chỉ giữ lại các dải sáng nhất

    // ---------------------------------------------------------
    // 4. PHA MÀU VÀ KÍCH SÁNG
    // ---------------------------------------------------------
    float alpha = wisp * mask * u_opacity * u_bodyColor.a;
    vec3 col = mix(u_bodyColor.rgb, u_glowColor.rgb, wisp);
    
    // Kích sáng nhân 2.5 lần để tạo hiệu ứng điện rực rỡ
    col *= 2.5; 

    finalColor = VFX_ResolveBody(col, 2.5, alpha);
}
