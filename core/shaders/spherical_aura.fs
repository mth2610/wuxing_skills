#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_scrollSpeed;
uniform float u_noiseScale;
uniform float u_fresnelPower; 

float vnoise3D(vec3 p) {
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
    for (int i = 0; i < 3; i++) {
        v += a * vnoise3D(p);
        p = p * 2.1 + vec3(13.7);
        a *= 0.5;
    }
    return v;
}

void main() {
    // ---- BƯỚC DEBUG (NẾU CẦN) ----
    // Nếu bạn bỏ comment dòng dưới cùng này mà game hiện lên một quả cầu MÀU ĐỎ CHÓT,
    // thì nghĩa là code C đã đúng 100%, lỗi nằm ở thuật toán pha màu bị tối.
    // finalColor = vec4(1.0, 0.0, 0.0, 1.0); return; 
    // ------------------------------

    vec3 normal  = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    vec3 dom = normal * u_noiseScale;
    float t = u_time * u_scrollSpeed;

    float n1 = fbm3(dom + vec3(t, -t * 0.6, t * 0.8));
    float n2 = fbm3(dom * 1.7 + vec3(-t * 0.7, t, -t * 0.5));
    
    float ridge = 1.0 - abs(2.0 * n1 - 1.0);
    
    // Đã tăng hệ số base (0.5) để sợi năng lượng sáng và dễ thấy hơn
    float wisp = ridge * (0.5 + 0.5 * n2);
    
    // Đã nới lỏng smoothstep (0.15 - 0.75) để không cắn mất chi tiết mờ
    wisp = smoothstep(0.15, 0.75, wisp);

    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    // Giữ cho tâm quả cầu luôn có ít nhất 20% độ mờ (mix 0.2), tránh tàng hình 100%
    float alpha = max(wisp, 0.05) * mix(0.2, 1.0, fresnel) * u_opacity * u_bodyColor.a;
    
    vec3 color = mix(u_bodyColor.rgb, u_glowColor.rgb, wisp);
    color += u_glowColor.rgb * fresnel * 0.8 * wisp;
    color *= 3.0;

    finalColor = vec4(color, alpha);
}