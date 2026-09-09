#version 330
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time;

out vec4 finalColor;

void main() {
    float circleAlpha = texture(texture0, fragTexCoord).r;

    vec2 warpUV = fragTexCoord;
    
    // Gió tạt ngang, xé rách UV nhẹ nhàng
    warpUV.x += sin(warpUV.y * 12.0 - u_time * 8.0) * 0.06;
    // Cuộn UV từ dưới lên trên tạo hiệu ứng lửa bốc
    vec2 flow = vec2(0.0, -u_time * 3.5);
    
    float n = vnoise(warpUV * 4.0 + flow);
    n += vnoise(warpUV * 8.0 - flow * 0.5) * 0.5;
    
    float density = circleAlpha * n * fragColor.r * 2.5;

    // Ghost of Tsushima: Bức xạ vật thể đen Planck theo nhiệt độ ngọn lửa
    float flameTemp = clamp(density / 1.1, 0.0, 1.0);
    vec3 mixedColor = calcBlackbodyNormalized(flameTemp);
    
    // TỐI ƯU 2: Khử nốt nhánh Rẽ nhánh 'if (circleAlpha < 0.05)' ban đầu bằng mặt nạ toán học
    float alphaOut = smoothstep(0.05, 0.4, density); 
    float visibilityMask = step(0.05, circleAlpha);

    // Drawn as VFX_RENDER_PASS_EMISSION / VFX_SURFACE_ADDITIVE (fire_skill.c),
    // so the emission resolver is the matching one. mask stays 1.0 because
    // alphaOut IS this effect's mask — feeding it to both would square it.
    finalColor = VFX_ResolveEmission(mixedColor, 1.0,
                                     1.0, alphaOut * fragColor.a * visibilityMask);
}