#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;

// Anamorphic streak (Đợt E1b) — a second, axis-STRETCHED tap set blended into
// the same target. Real anamorphic lenses smear highlights along one axis; that
// horizontal bar through every bright point is most of what makes a frame read
// as "cinematic" rather than "a game with bloom". Done here in the downsample
// (rather than as its own pyramid) so it costs one tap loop at 1/8 res.
uniform float u_streakEnabled;
uniform float u_streakStrength; // 0..1 blend of streaked vs round
uniform float u_streakAngle;    // radians, 0 = horizontal (classic)

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    // 4-tap box at ±1 texel corners; bilinear HW gives free 4-tap each sample.
    // Produces a wide, alias-free downsample pass for the dual-filter pyramid.
    vec4 sum = texture(texture0, uv + vec2(-1.0, -1.0) * u_texelSize)
             + texture(texture0, uv + vec2( 1.0, -1.0) * u_texelSize)
             + texture(texture0, uv + vec2(-1.0,  1.0) * u_texelSize)
             + texture(texture0, uv + vec2( 1.0,  1.0) * u_texelSize);
    vec4 roundCol = sum * 0.25;

    if (u_streakEnabled > 0.5) {
        // Stretched ALONG the streak axis, compressed across it — that anisotropy
        // is the whole effect. A symmetric kernel here would just be more bloom.
        vec2 dir  = vec2(cos(u_streakAngle), sin(u_streakAngle));
        vec2 perp = vec2(-dir.y, dir.x);
        vec4 streak = vec4(0.0);
        for (int i = -3; i <= 3; i++) {
            vec2 off = dir * (float(i) * 4.0) * u_texelSize
                     + perp * 0.25 * u_texelSize;
            streak += texture(texture0, uv + off);
        }
        streak /= 7.0;
        roundCol = mix(roundCol, streak, u_streakStrength);
    }

    finalColor = roundCol;
}
