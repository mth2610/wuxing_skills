#version 330

// AUTO EXPOSURE — one fragment, one pass, no CPU readback.

in vec2 fragTexCoord;

uniform sampler2D texture0;      // the 1x1 PREVIOUS exposure
uniform sampler2D u_lumaTex;     // the background-luma target
uniform vec2  u_lumaSize;
uniform float u_dt;
uniform float u_targetGrey;      // where the metered average should land
uniform float u_minExposure;     // art-approved floor
uniform float u_speedDown;       // adapting toward a BRIGHTER scene (exposure falls)
uniform float u_speedUp;         // adapting toward a DARKER scene (exposure rises)

out vec4 finalColor;

void main() {
    const int N = 12;
    const float invN = 1.0 / float(N);
    float log2Sum = 0.0;
    float wSum = 0.0;

    for (int y = 0; y < N; y++) {
        float vy = (float(y) + 0.5) * invN;
        float dy = abs(vy - 0.5) * 2.0;
        for (int x = 0; x < N; x++) {
            float vx = (float(x) + 0.5) * invN;
            float dx = abs(vx - 0.5) * 2.0;

            float w = (1.0 - smoothstep(0.55, 1.0, max(dx, dy))) * 0.9 + 0.1;
            float l = clamp(texture(u_lumaTex, vec2(vx, vy)).r, 1e-4, 64.0);
            // Dùng log2/exp2 gốc của phần cứng GPU thay cho log/exp tự nhiên
            log2Sum += log2(l) * w;
            wSum += w;
        }
    }
    float avg = exp2(log2Sum / max(wSum, 1e-4));

    float minExp = clamp(u_minExposure, 0.01, 1.0);
    float target = clamp(u_targetGrey / max(avg, 1e-4), minExp, 1.0);

    float prev = texture(texture0, vec2(0.5)).r;
    // Dùng phủ định !(prev > 0.0 && prev <= 1.0) để bắt cả trường hợp NaN/Inf trong IEEE-754
    if (!(prev > 0.0 && prev <= 1.0)) prev = 1.0;

    float rate = (target < prev) ? u_speedDown : u_speedUp;
    float k = 1.0 - exp(-max(rate, 0.0) * clamp(u_dt, 0.0, 0.25));
    float e = clamp(mix(prev, target, clamp(k, 0.0, 1.0)), minExp, 1.0);

    finalColor = vec4(e, e, e, 1.0);
}