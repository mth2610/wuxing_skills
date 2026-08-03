#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/uv/shaders/uv_deform.glsl"

uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_fresnelPower;
uniform float u_rimStrength;
uniform float u_scrollSpeed;
uniform float u_noiseScale;
uniform float u_heightScale;
uniform float u_scanFreq;
uniform float u_scanSpeed;
uniform float u_scanStrength;

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
    vec3 nrm     = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    float fresnel = calcFresnel(nrm, viewDir, u_fresnelPower);

    // NOT folded, deliberately. This feeds fbm3 — an APERIODIC domain — so
    // wrapping the clock would make the whole wisp field jump once per cycle.
    // The fold that core/uv offers is exact for a sine and for a REPEAT-wrapped
    // sampler, and for nothing else; the scan below qualifies and this does not.
    float yScroll = fragPosition.y * u_heightScale - u_time * u_scrollSpeed;
    vec3 dom = vec3(nrm.x * u_noiseScale, yScroll, nrm.z * u_noiseScale);

    float n1    = fbm3(dom);
    float n2    = fbm3(dom * 1.6 + vec3(3.7, -2.1, 5.3));
    float ridge = 1.0 - abs(2.0 * n1 - 1.0);
    float wisp  = ridge * (0.3 + 0.7 * n2);
    wisp = smoothstep(0.25, 0.75, wisp);
    
    // Periodic, so the clock CAN be folded — and must be, or u_time*u_scanSpeed
    // outgrows float32's ability to resolve a fraction of a cycle and the scan
    // stutters after a long session. Keeps the shader's own radian units.
    float scanRaw = 0.5 + 0.5 * sin(UVDeform_FoldAngle(
                        fragPosition.y * u_scanFreq - u_time * u_scanSpeed));
    float scan    = scanRaw * scanRaw * u_scanStrength;
    float filmAlpha = wisp * 0.75 + scan * 0.30 + fresnel * 0.20;
    filmAlpha = clamp(filmAlpha, 0.0, 1.0);
    
    // Alpha tổng ban đầu
    float alpha = filmAlpha * u_opacity * u_bodyColor.a;

    // ----------------------------------------------------------------------
    // ÉP MỜ TỪ DƯỚI LÊN TRÊN DÙNG CHUẨN HEADER MỚI XÁC NHẬN
    // Tọa độ V (fragTexCoord.y) chạy từ 0.0 (đáy) lên 1.0 (đỉnh)
    // Phép tính (1.0 - fragTexCoord.y) giữ nguyên 100% màu ở đáy và mờ dần về 0% ở đỉnh
    // ----------------------------------------------------------------------
    alpha *= (1.0 - fragTexCoord.y);

    float glowBlend = clamp(wisp * 0.5 + scan * 0.2, 0.0, 1.0);
    vec3  col       = mix(u_bodyColor.rgb, u_glowColor.rgb, glowBlend);
    col += u_glowColor.rgb * fresnel * u_rimStrength * 0.5;

    finalColor = vec4(col, alpha);
}