#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D causticsTex; // caustics
uniform sampler2D flowTex;     // flowmap
uniform float u_time;

out vec4 finalColor;

#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/vfx_composite.glsl"

void main() {
    vec2 centerDist = fragTexCoord - vec2(0.5);
    float dist = length(centerDist);
    float angle = atan(centerDist.y, centerDist.x);
    
    // 1. ĐỒNG BỘ VIỀN: Tái tạo lại công thức biên dạng hữu cơ từ code C
    // Công thức bên C: n = 1.0 + 0.15*sin(a*3+t) + 0.1*cos(a*5-t*0.5)
    float n = 1.0 + 0.15 * sin(angle * 3.0 + u_time) + 0.1 * cos(angle * 5.0 - u_time * 0.5);
    float maxDist = 0.5 * n; // Khoảng cách thực tế từ tâm đến viền mesh tại góc này
    
    // Chuẩn hóa khoảng cách (0.0 tại tâm, 1.0 đúng ngay tại viền mesh hữu cơ)
    float normalizedDist = dist / maxDist;
    
    // An toàn: Cắt bỏ rác ngoài viền
    if (normalizedDist > 1.0) discard;
    
    // 2. NHIỄU DÒNG CHẢY (Flow map blending)
    vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
    
    // Làm cong UV một chút để caustics trôi dập dềnh theo mặt nước
    vec2 distortedUV = fragTexCoord + centerDist * sin(u_time * 0.8) * 0.05;
    
    float caust1 = flowBlend(causticsTex, distortedUV * 3.0, flowDir, 0.5, 0.15, u_time);
    float caust2 = flowBlend(causticsTex, distortedUV * 5.0 + 0.3, flowDir, -0.4, 0.1, u_time);
    float caustic = caust1 * 0.7 + caust2 * 0.5;
    
    // 3. XÂY DỰNG CHIỀU SÂU VÀ VIỀN MA THUẬT (Depth & Magic Rim)
    // Vực sâu (Depth): Tạo cảm giác ở giữa tâm nước sâu và tối hơn, ngoài rìa cạn và sáng
    float depthMask = smoothstep(0.0, 0.7, normalizedDist);
    
    // Viền phát sáng rực rỡ bám sát theo đường cong lồi lõm
    float rimGlow = smoothstep(0.7, 0.95, normalizedDist) * smoothstep(1.0, 0.9, normalizedDist);
    
    // Fade out cực êm ở rìa ngoài cùng để tan mượt vào mặt đất
    float edgeFade = smoothstep(1.0, 0.85, normalizedDist);
    
    // 4. PHỐI MÀU (Color Mixing)
    vec3 deepColor = vec3(0.01, 0.15, 0.4);   // Màu xanh thẳm ở giữa
    vec3 shallowColor = vec3(0.0, 0.6, 1.0);  // Màu xanh dương ngọc ở rìa cạn
    
    // Trộn màu từ tâm ra viền
    vec3 baseColor = mix(deepColor, shallowColor, depthMask);
    
    // Cộng lớp Caustics (Lấp lánh mạnh hơn ở chỗ nước cạn/rìa ngoài)
    baseColor += vec3(0.4, 0.9, 1.0) * caustic * (0.3 + depthMask * 1.5);
    
    // Cộng viền ma thuật (Viền năng lượng Plasma/Teal rực rỡ)
    baseColor += vec3(0.2, 0.8, 1.0) * rimGlow * 2.5;
    
    // Tính toán Alpha cuối cùng bám sát hình thái mesh
    float finalAlpha = fragColor.a * edgeFade;
    
    finalColor = VFX_ResolveBody(baseColor, 1.0, finalAlpha);
}
