#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;

// Firefly control (replaces the old hard energy clamp in bloom_bright.fs).
// 1.0 = weight each tap group by 1/(1+luma) before averaging, so ONE isolated
// blazing texel cannot dominate the mip it lands in. The weights are
// renormalised, which is the whole point: a large uniformly-bright area comes
// out UNCHANGED (every group carries the same weight), while a lone spike is
// pulled down. That is what lets the bright pass hand over unclamped HDR — the
// old `length()` clamp had to cap every pixel because nothing downstream could
// tell a firefly from a genuinely bright core.
// Only meaningful on the FIRST downsample (full-rate source); later mips are
// already averaged, so the caller passes 0.0 there.
uniform float u_karis;

// Anamorphic streak (Đợt E1b) — a second, axis-STRETCHED tap set blended into
// the same target. Real anamorphic lenses smear highlights along one axis; that
// horizontal bar through every bright point is most of what makes a frame read
// as "cinematic" rather than "a game with bloom". Done here in the downsample
// (rather than as its own pyramid) so it costs one tap loop.
uniform float u_streakEnabled;
uniform float u_streakStrength; // 0..1 blend of streaked vs round
uniform float u_streakAngle;    // radians, 0 = horizontal (classic)

out vec4 finalColor;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    vec2 uv = fragTexCoord;
    vec2 t = u_texelSize;

    // 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of
    // Duty AAB"). The old 4-tap box aliased badly once the pyramid got deep:
    // each level re-sampled a grid that had already lost half its detail, so a
    // thin bright line turned into a dotted one that crawled as the camera
    // moved. The overlapping 3x3 + centre-2x2 footprint is what keeps a deep
    // pyramid stable.
    //   a b c
    //    j k
    //   d e f
    //    l m
    //   g h i
    vec3 a = texture(texture0, uv + vec2(-2.0,  2.0) * t).rgb;
    vec3 b = texture(texture0, uv + vec2( 0.0,  2.0) * t).rgb;
    vec3 c = texture(texture0, uv + vec2( 2.0,  2.0) * t).rgb;
    vec3 d = texture(texture0, uv + vec2(-2.0,  0.0) * t).rgb;
    vec3 e = texture(texture0, uv                        ).rgb;
    vec3 f = texture(texture0, uv + vec2( 2.0,  0.0) * t).rgb;
    vec3 g = texture(texture0, uv + vec2(-2.0, -2.0) * t).rgb;
    vec3 h = texture(texture0, uv + vec2( 0.0, -2.0) * t).rgb;
    vec3 i = texture(texture0, uv + vec2( 2.0, -2.0) * t).rgb;
    vec3 j = texture(texture0, uv + vec2(-1.0,  1.0) * t).rgb;
    vec3 k = texture(texture0, uv + vec2( 1.0,  1.0) * t).rgb;
    vec3 l = texture(texture0, uv + vec2(-1.0, -1.0) * t).rgb;
    vec3 m = texture(texture0, uv + vec2( 1.0, -1.0) * t).rgb;

    // Five overlapping 2x2 groups: the inner one carries half the weight.
    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25;
    vec3 g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25;
    vec3 g4 = (e + f + h + i) * 0.25;

    float w0 = 0.5, w1 = 0.125, w2 = 0.125, w3 = 0.125, w4 = 0.125;
    if (u_karis > 0.5) {
        w0 /= (1.0 + Luma(g0));
        w1 /= (1.0 + Luma(g1));
        w2 /= (1.0 + Luma(g2));
        w3 /= (1.0 + Luma(g3));
        w4 /= (1.0 + Luma(g4));
    }
    // Renormalising is what makes this energy-preserving for uniform input.
    float wSum = w0 + w1 + w2 + w3 + w4;
    vec3 roundCol = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(wSum, 1e-5);

    if (u_streakEnabled > 0.5) {
        // Stretched ALONG the streak axis, compressed across it — that anisotropy
        // is the whole effect. A symmetric kernel here would just be more bloom.
        vec2 dir  = vec2(cos(u_streakAngle), sin(u_streakAngle));
        vec2 perp = vec2(-dir.y, dir.x);
        vec3 streak = vec3(0.0);
        for (int s = -3; s <= 3; s++) {
            vec2 off = dir * (float(s) * 4.0) * t + perp * 0.25 * t;
            streak += texture(texture0, uv + off).rgb;
        }
        streak /= 7.0;
        roundCol = mix(roundCol, streak, u_streakStrength);
    }

    // Alpha is pinned to 1.0 so the upsample chain's alpha-lerp blend is exact:
    // the blend factor must come from the draw tint alone, never from whatever
    // alpha happened to survive the pyramid.
    finalColor = vec4(roundCol, 1.0);
}
