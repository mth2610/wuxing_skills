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

void main() {
    vec2 uv = fragTexCoord;
    vec2 t = u_texelSize;
    vec2 t2 = t * 2.0;

    // Core 5-tap Dual-Kawase samples (center + 4 inner bilinear diagonals)
    vec3 e = texture(texture0, uv                     ).rgb;
    vec3 j = texture(texture0, uv + vec2( -t.x,   t.y)).rgb;
    vec3 k = texture(texture0, uv + vec2(  t.x,   t.y)).rgb;
    vec3 l = texture(texture0, uv + vec2( -t.x,  -t.y)).rgb;
    vec3 m = texture(texture0, uv + vec2(  t.x,  -t.y)).rgb;

    vec3 roundCol;
    if (u_karis > 0.5) {
        // Full 13-tap Jimenez + Karis firefly control (chỉ bật ở mip đầu tiên)
        vec3 a = texture(texture0, uv + vec2(-t2.x,  t2.y)).rgb;
        vec3 b = texture(texture0, uv + vec2(  0.0,  t2.y)).rgb;
        vec3 c = texture(texture0, uv + vec2( t2.x,  t2.y)).rgb;
        vec3 d = texture(texture0, uv + vec2(-t2.x,   0.0)).rgb;
        vec3 f = texture(texture0, uv + vec2( t2.x,   0.0)).rgb;
        vec3 g = texture(texture0, uv + vec2(-t2.x, -t2.y)).rgb;
        vec3 h = texture(texture0, uv + vec2(  0.0, -t2.y)).rgb;
        vec3 i = texture(texture0, uv + vec2( t2.x, -t2.y)).rgb;

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
        // Fast 5-tap Dual-Kawase downsample cho các tầng mip tiếp theo (giảm 61.5% texture fetches)
        roundCol = e * 0.5 + (j + k + l + m) * 0.125;
    }

    if (u_streakEnabled > 0.5) {
        vec2 dir  = vec2(cos(u_streakAngle), sin(u_streakAngle));
        vec2 perp = vec2(-dir.y, dir.x);
        vec2 baseUV = uv + (perp * 0.25) * t;
        vec2 stepUV = (dir * 4.0) * t;

        vec3 streak = texture(texture0, baseUV - stepUV * 3.0).rgb
                    + texture(texture0, baseUV - stepUV * 2.0).rgb
                    + texture(texture0, baseUV - stepUV      ).rgb
                    + texture(texture0, baseUV               ).rgb
                    + texture(texture0, baseUV + stepUV      ).rgb
                    + texture(texture0, baseUV + stepUV * 2.0).rgb
                    + texture(texture0, baseUV + stepUV * 3.0).rgb;

        roundCol = mix(roundCol, streak * (1.0 / 7.0), u_streakStrength);
    }

    finalColor = vec4(roundCol, 1.0);
}