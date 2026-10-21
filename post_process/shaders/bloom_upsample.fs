#version 330
#ifdef GL_ES
precision highp float;
#endif

in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform vec2 u_texelSize;
// How much of this (wider) level replaces the (tighter) level it is being
// folded into. The caller draws with BLEND_ALPHA, so emitting it as the
// fragment's ALPHA makes the hardware compute mix(dst, tent, u_scatter)
// exactly. It is a uniform rather than the draw tint's alpha on purpose: the
// tint would have to survive vertex-colour plumbing through both backends to
// mean anything, and if it silently arrived as 1.0 the fold would become an
// overwrite again — the exact bug this pass was rewritten to fix, and an
// invisible one, because an overwrite still produces a plausible-looking glow.
uniform float u_scatter;

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    vec2 d = u_texelSize * 0.5;

    // 4 bilinear taps with half-texel offset evaluate the exact 3x3 tent filter
    // (weights 1,2,1 / 2,4,2 / 1,2,1) using hardware texture filtering.
    // Cuts texture fetches from 9 down to 4 taps per fragment (-55% bandwidth).
    vec3 sum = (texture(texture0, uv + vec2(-d.x,  d.y)).rgb
              + texture(texture0, uv + vec2( d.x,  d.y)).rgb
              + texture(texture0, uv + vec2(-d.x, -d.y)).rgb
              + texture(texture0, uv + vec2( d.x, -d.y)).rgb) * 4.0;

    finalColor = vec4(sum / 16.0, clamp(u_scatter, 0.0, 1.0));
}
