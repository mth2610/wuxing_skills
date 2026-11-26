#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;
uniform float u_karis;
uniform float u_streakEnabled;
uniform float u_streakStrength;
uniform float u_streakAngle;

out vec4 finalColor;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 SafeTap(vec2 uv) {
    return clamp(texture(texture0, uv).rgb, vec3(0.0), vec3(65000.0));
}

vec3 StreakTap(vec2 uv) {
    vec3 c = SafeTap(uv);
    // Nén nhẹ các điểm quá chói để vệt anamorphic mượt, không tạo đốm rời rạc
    return c / (1.0 + Luma(c) * 0.15);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 t = u_texelSize;
    vec2 t2 = t * 2.0;

    // Core 5-tap Dual-Kawase samples (center + 4 inner bilinear diagonals)
    vec3 e = SafeTap(uv);
    vec3 j = SafeTap(uv + vec2( -t.x,   t.y));
    vec3 k = SafeTap(uv + vec2(  t.x,   t.y));
    vec3 l = SafeTap(uv + vec2( -t.x,  -t.y));
    vec3 m = SafeTap(uv + vec2(  t.x,  -t.y));

    vec3 roundCol;
    if (u_karis > 0.5) {
        // Full 13-tap Jimenez + Karis firefly control (chỉ bật ở mip đầu tiên)
        vec3 a = SafeTap(uv + vec2(-t2.x,  t2.y));
        vec3 b = SafeTap(uv + vec2(  0.0,  t2.y));
        vec3 c = SafeTap(uv + vec2( t2.x,  t2.y));
        vec3 d = SafeTap(uv + vec2(-t2.x,   0.0));
        vec3 f = SafeTap(uv + vec2( t2.x,   0.0));
        vec3 g = SafeTap(uv + vec2(-t2.x, -t2.y));
        vec3 h = SafeTap(uv + vec2(  0.0, -t2.y));
        vec3 i = SafeTap(uv + vec2( t2.x, -t2.y));

        vec3 g0 = (j + k + l + m) * 0.25;
        vec3 g1 = (a + b + d + e) * 0.25;
        vec3 g2 = (b + c + e + f) * 0.25;
        vec3 g3 = (d + e + g + h) * 0.25;
        vec3 g4 = (e + f + h + i) * 0.25;

        float w0 = 0.5   / (1.0 + Luma(g0));
        float w1 = 0.125 / (1.0 + Luma(g1));
        float w2 = 0.125 / (1.0 + Luma(g2));
        float w3 = 0.125 / (1.0 + Luma(g3));
        float w4 = 0.125 / (1.0 + Luma(g4));

        float invWSum = 1.0 / max(w0 + w1 + w2 + w3 + w4, 1e-5);
        roundCol = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) * invWSum;
    } else {
        // Fast 5-tap Dual-Kawase downsample cho các tầng mip tiếp theo
        roundCol = e * 0.5 + (j + k + l + m) * 0.125;
    }

    if (u_streakEnabled > 0.5) {
        vec2 dir  = vec2(cos(u_streakAngle), sin(u_streakAngle));
        vec2 perp = vec2(-dir.y, dir.x);
        vec2 baseUV = uv + (perp * 0.25) * t;
        vec2 stepUV = (dir * 2.5) * t;

        // Trọng số Gaussian giảm dần từ tâm (tổng = 0.22 + 2*(0.18 + 0.13 + 0.08) = 1.0)
        // giúp tia sáng liền mạch, loại bỏ hiện tượng đứt hạt (beading) của box filter.
        vec3 streak = StreakTap(baseUV) * 0.22
                    + (StreakTap(baseUV - stepUV)       + StreakTap(baseUV + stepUV))       * 0.18
                    + (StreakTap(baseUV - stepUV * 2.0) + StreakTap(baseUV + stepUV * 2.0)) * 0.13
                    + (StreakTap(baseUV - stepUV * 3.0) + StreakTap(baseUV + stepUV * 3.0)) * 0.08;

        roundCol = mix(roundCol, streak, clamp(u_streakStrength, 0.0, 1.0));
    }

    finalColor = vec4(roundCol, 1.0);
}