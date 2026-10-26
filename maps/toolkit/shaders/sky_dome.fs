#version 330
#ifdef GL_ES
precision highp float;
#endif
in vec3 skyRay;
uniform vec3 u_sunDirection;
uniform vec3 u_skyAmbient;
uniform vec3 u_hazeColor;
uniform vec3 u_sunColor;
out vec4 finalColor;
#include "maps/toolkit/shaders/map_sky.glsl"
void main()
{
    // Chuẩn hóa an toàn u_sunDirection để đảm bảo dot(ray, sun) luôn chính xác tuyệt đối cho đĩa mặt trời
    float sunLenSq = dot(u_sunDirection, u_sunDirection);
    vec3 safeSunDir = (sunLenSq > 1e-8) ? (u_sunDirection * inversesqrt(sunLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 skyColor = MapSkyRadiance(skyRay, safeSunDir, u_skyAmbient, u_hazeColor, u_sunColor);

    // [VISUAL UPGRADE]: Khử sọc chuyển sắc 8-bit (Color Banding) trên vòm trời bằng nhiễu Interleaved Gradient Noise
    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    skyColor = max(skyColor + (ign - 0.5) * (1.0 / 255.0), vec3(0.0));

    finalColor = vec4(skyColor, 1.0);
}
