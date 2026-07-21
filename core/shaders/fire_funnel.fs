#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec4  u_baseColor;
uniform float u_emissiveIntensity;
uniform float u_customParam1;

#define ITER 10
#define SPL 8.0
#define DISP 0.35
#define YS 0.3

float n3(vec3 x) {
    vec3 p = floor(x), f = fract(x);
    f = f*f*(3.0-2.0*f);
    float a = hash3(p), b = hash3(p+vec3(1,0,0)), c = hash3(p+vec3(0,1,0)), d = hash3(p+vec3(1,1,0));
    float e = hash3(p+vec3(0,0,1)), g = hash3(p+vec3(1,0,1)), h = hash3(p+vec3(0,1,1)), j = hash3(p+vec3(1,1,1));
    return mix(mix(mix(a,b,f.x),mix(c,d,f.x),f.y),mix(mix(e,g,f.x),mix(h,j,f.x),f.y),f.z)*2.0-1.0;
}

void main() {
    float h = fragTexCoord.y, u = fragTexCoord.x;
    vec2 uv = vec2(u - 0.5, h);

    float nx = 0.0, ny = 0.0;
    for (int i = 1; i <= ITER; i++) {
        float ii = float(i*i);
        float fr = float(i)/float(ITER);
        float t = fr * u_time * SPL;
        float d = (1.0 - fr) * DISP;
        nx += n3(vec3(uv.x*ii - u_time*fr, uv.y*YS*ii - t, u_time*fr)) * d * 2.0;
        ny += n3(vec3(uv.x*ii + u_time*fr, uv.y*YS*ii - t, u_time*fr/ii)) * d;
    }

    float flame = 1.0 - clamp(h + ny, 0.0, 1.5);
    flame = clamp(flame + nx*0.25, 0.0, 1.0);

    vec3 white  = vec3(1.0, 0.92, 0.55);
    vec3 orange = vec3(1.0, 0.5, 0.07);
    vec3 red    = vec3(0.7, 0.1, 0.03);
    vec3 dark   = vec3(0.15, 0.02, 0.005);

    vec3 col;
    if (flame > 0.7)
        col = mix(orange, white, (flame - 0.7) / 0.3);
    else if (flame > 0.35)
        col = mix(red, orange, (flame - 0.35) / 0.35);
    else
        col = mix(dark, red, flame / 0.35);

    float noiseA = n3(vec3(u*5.0 + u_time*0.3, h*8.0 - u_time*0.5, u_time*0.15));
    float alpha = smoothstep(0.15, 0.65, flame);
    alpha *= 1.0 - smoothstep(0.05, 0.75, h);
    alpha *= 0.7 + noiseA*0.15;
    alpha = clamp(alpha, 0.0, 1.0);

    col *= u_emissiveIntensity + 0.5;

    finalColor = vec4(col * u_baseColor.rgb, alpha);
}
