#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4  u_baseColor;
uniform float u_emissiveIntensity;
uniform float u_customParam1;

#define OCTAVES     5
#define LACUNARITY  2.0
#define GAIN        0.5
#define BASE_DISP   0.35
#define YS          0.3
#define TWO_PI      6.28318530718

float n3(vec3 x) {
    vec3 p = floor(x), f = fract(x);
    f = f*f*(3.0-2.0*f);
    float a = hash3(p),             b = hash3(p+vec3(1,0,0));
    float c = hash3(p+vec3(0,1,0)), d = hash3(p+vec3(1,1,0));
    float e = hash3(p+vec3(0,0,1)), g = hash3(p+vec3(1,0,1));
    float h = hash3(p+vec3(0,1,1)), j = hash3(p+vec3(1,1,1));
    return mix(mix(mix(a,b,f.x),mix(c,d,f.x),f.y),
               mix(mix(e,g,f.x),mix(h,j,f.x),f.y), f.z) * 2.0 - 1.0;
}

void main() {
    float h = fragTexCoord.y;
    float u = fragTexCoord.x;

    float angle = u * TWO_PI;
    vec2 circ = vec2(cos(angle), sin(angle)) * 1.6; // seamless quanh phễu

    // turb chỉ dao động nhẹ quanh 1.0 — không cho khuếch đại mất kiểm soát
    float turb = 1.0 + clamp(u_customParam1, -1.0, 1.0) * 0.35;

    // Chuẩn hoá tổng biên độ FBM về đúng BASE_DISP, bất kể OCTAVES/turb
    float ampSum = BASE_DISP * (1.0 - pow(GAIN, float(OCTAVES))) / (1.0 - GAIN);
    float normFactor = BASE_DISP / ampSum;

    float nx = 0.0, ny = 0.0;
    float amp  = BASE_DISP;
    float freq = 1.0;

    for (int i = 0; i < OCTAVES; i++) {
        float t = u_time * (2.0 + float(i) * 1.2);
        nx += n3(vec3(circ.x*freq - u_time*0.6, h*YS*freq - t, u_time*0.3)) * amp;
        ny += n3(vec3(circ.y*freq + u_time*0.6, h*YS*freq - t, u_time*0.3/max(freq,0.001))) * amp * 0.6;
        freq *= LACUNARITY;
        amp  *= GAIN;
    }
    nx *= normFactor * turb;
    ny *= normFactor * turb;

    float flame = 1.0 - clamp(h + ny, 0.0, 1.5);
    flame = clamp(flame + nx * 0.25, 0.0, 1.0);

    // Noise chi tiết riêng cho MÀU — không đụng alpha, chống "phẳng đều"
    float colorDetail = n3(vec3(circ.x*6.0, h*10.0 - u_time*1.2, u_time*0.4)) * 0.15;
    float flameColor = clamp(flame + colorDetail, 0.0, 1.0);

    vec3 white  = vec3(1.0, 0.92, 0.55);
    vec3 orange = vec3(1.0, 0.5, 0.07);
    vec3 red    = vec3(0.7, 0.1, 0.03);
    vec3 dark   = vec3(0.15, 0.02, 0.005);

    // Color ramp branchless — không if/else
    vec3 col = dark;
    col = mix(col, red,    smoothstep(0.0,  0.35, flameColor));
    col = mix(col, orange, smoothstep(0.35, 0.7,  flameColor));
    col = mix(col, white,  smoothstep(0.7,  1.0,  flameColor));

    float noiseA = n3(vec3(circ.x*2.5 + u_time*0.3, h*8.0 - u_time*0.5, u_time*0.15));
    float alpha = smoothstep(0.15, 0.65, flame);
    alpha *= 1.0 - smoothstep(0.05, 0.75, h);
    alpha *= 0.7 + noiseA * 0.15;
    alpha = clamp(alpha, 0.0, 1.0);

    col *= u_emissiveIntensity + 0.5;

    finalColor = vec4(col * u_baseColor.rgb, alpha);
}