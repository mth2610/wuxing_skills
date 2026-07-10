#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4  u_color;
uniform float u_progress;
uniform float u_diffusion;
uniform float u_noiseScale;
uniform float u_driftSpeed;
uniform vec2  u_sourcePos;   // origin of the puff in quad-local uv space, {0,0} = center
                              // (radial shockwave-ring puff only — see core/shaders/smoke_column.fs
                              // for the separate rising-column shader; a single shared shader
                              // trying to do both a static-point ring AND a climbing column via
                              // uniforms was hard to tune independently, so they were split)

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

    float t0 = 0.005;
    float t  = mix(t0, 1.0, clamp(u_progress, 0.0, 1.0));
    
    float D  = max(u_diffusion * 0.1, 0.001); 
    float variance = 4.0 * D * t;
    
    // ĐIỀU CHỈNH 1: Kéo dài tuổi thọ khói. Thay pow(..., 1.5) bằng pow(..., 2.5)
    // Mũ càng cao thì khói càng giữ độ đậm ở giai đoạn giữa-cuối lâu hơn và tan từ từ
    float artisticFade = 1.0 - pow(u_progress, 2.5); 
    float ampNorm = mix(1.0, artisticFade, u_progress);

    // Macro & Micro Warp (Giữ nguyên độ nhiễu loạn hình học)
    vec3 dom0 = vec3(uv * (u_noiseScale * 0.3), u_time * u_driftSpeed * 0.8);
    vec2 n0 = vec2(fbm3(dom0), fbm3(dom0 + vec3(7.2, 1.1, 3.4))) - 0.5;

    vec3 dom1 = vec3((uv + n0 * 0.6) * u_noiseScale, u_time * u_driftSpeed);
    vec2 n1 = vec2(fbm3(dom1), fbm3(dom1 + vec3(5.2, 1.3, 0.0))) - 0.5;

    float warpStrength = mix(0.2, 1.2, clamp(u_progress, 0.0, 1.0));
    vec2 uvw = uv + (n0 * 0.7 + n1 * 0.3) * warpStrength;
    float dist = length(uvw - u_sourcePos);

    // Bán kính hỗn loạn
    float pushRadius = mix(0.0, 0.35, pow(u_progress, 0.4));
    float chaoticPush = pushRadius + n0.x * 0.6 * u_progress;

    float ringDist = abs(dist - chaoticPush);
    float r2 = ringDist * ringDist;

    float intensity = 1.6; 
    float baseDensity = ampNorm * exp(-r2 / variance) * intensity;
    baseDensity = 1.0 - exp(-baseDensity * 1.5);

    // ĐIỀU CHỈNH 2: Giảm nhẹ lực bào mòn ở cuối vòng đời
    // Nhân thêm (1.0 - u_progress) để khi progress tiến về 1, nhiễu không gặm quá bạo lực
    float edgeNoise = fbm3(vec3(uv * (u_noiseScale * 2.5), u_time * u_driftSpeed * 0.5));
    float erosionMask = smoothstep(0.0, 0.3, u_progress) * (1.0 - u_progress * 0.5); 
    float erosion = edgeNoise * smoothstep(0.0, 0.6, r2) * u_progress * erosionMask;
    
    float centerErosion = smoothstep(chaoticPush, 0.0, dist) * u_progress;
    
    float density = max(baseDensity - erosion * 1.4 - centerErosion * 0.9, 0.0);

    float alpha = clamp(density, 0.0, 1.0);
    
    // ĐIỀU CHỈNH 3: Thay đổi bộ bo biên Quad (Fade out an toàn)
    // Thay vì dùng smoothstep cắt thẳng, ta dùng hàm smoothstep rộng từ 0.3 đến 1.0
    // để khói cứ bay ra xa là tự động tan biến hòa vào không khí, không bao giờ bị lộ mép hình vuông.
    float softEdge = 1.0 - smoothstep(0.3, 1.0, length(uv));
    alpha *= softEdge; 
    
    alpha *= smoothstep(0.0, 0.05, u_progress);

    finalColor = vec4(u_color.rgb, alpha * u_color.a);
}