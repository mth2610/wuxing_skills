#version 330
#include "core/shaders/common/fs_header.glsl"

// ── TRAIL DEFORM FRAGMENT — packed 4-channel wisp material ──────────────────
// ONE RGBA texture carries four roles (the "packed" convention), so one bind
// and one asset replace the sheet + flow map + mask trio:
//   R = coarse wisp shape   G = fine wisp detail
//   B = dissolve noise      A = high-frequency turbulence
//
// Two samples of the SAME texture (one bind, one asset, cache-friendly):
// R+B ride the coarse pan, G+A ride the fine pan, so the coarse and fine wisp
// layers scroll at different speeds — the internal motion that makes the
// strip read as thick matter, not a flat sticker. A-channel turbulence
// modulates the coarse<->fine mix (self-referential ripple of the wisp edge,
// no third sample needed), and B pans the dissolve edge so the tail erodes
// organically as the noise field moves through it.
//
// Output feeds BOTH passes with the same formula — BLEND_ALPHA body (colored
// material, VFX body layer) and BLEND_ADDITIVE emission (glow, emission
// layer) both consume src.rgb * src.a — so one shader serves the
// bright-background contract without a per-skill render branch.

uniform sampler2D texture0;
uniform float u_matMode;        // 0 = passthrough, 1 = packed wisp
uniform float u_wispMix;        // coarse(R) -> fine(G)
uniform float u_dissolve;       // B-channel dissolve threshold
uniform float u_dissolveSoft;   // dissolve edge softness
uniform float u_edgeTear;       // 0 = off; fine-noise jitter of the dissolve
                                // threshold, strongest at the band edges —
                                // the silhouette frays ("rách") like smoke
uniform float u_turbStrength;   // A-channel mix jitter
uniform vec2  u_tiling;         // x = tiles along path, y = tiles across width
uniform vec4  u_panSpeed;       // x = coarse pan, y = fine pan (UV units/sec)
uniform float u_tailFadeA;      // segment fade start (0 = head, 1 = tail)
uniform float u_tailFadeB;      // segment fade end — tail dissolves to zero here
                                // (tailFadeA >= tailFadeB disables the ramp)

in vec2 vSegUV;
in vec4 vColor;

void main()
{
    if (u_matMode < 0.5)
    {
        finalColor = texture(texture0, vSegUV) * vColor;
        return;
    }

    vec2 uv = vec2(vSegUV.x * u_tiling.y, vSegUV.y * u_tiling.x);
    vec4 texC = texture(texture0, vec2(uv.x, uv.y + u_time * u_panSpeed.x));
    vec4 texF = texture(texture0, vec2(uv.x, uv.y + u_time * u_panSpeed.y));

    float turb = clamp(u_turbStrength * (texF.a - 0.5) * 2.0, -1.0, 1.0);
    float mixW = clamp(u_wispMix + turb, 0.0, 1.0);
    float wisp = mix(texC.r, texF.g, mixW);

    float edge = max(u_dissolveSoft, 0.001);
    // Torn silhouette: the dissolve threshold wobbles with fine noise, scaled
    // to zero at the band CENTRE and full at both edges, so the outline gets
    // irregular bites instead of a smooth gradient — the smoke frays at its
    // boundaries the way the tail frays at its end.
    float across = abs(vSegUV.x - 0.5f) * 2.0f;
    float edgeBias = smoothstep(0.12f, 0.45f, across);
    float thresh = u_dissolve + (texF.g - 0.5f) * 2.0f * u_edgeTear * edgeBias;
    float dissolveMask = smoothstep(thresh, thresh + edge, texC.b);

    // Segment-space tail ramp: the smoke widens toward the tail, then the
    // material dissolves it away instead of ending in a hard band.
    float tailMask = (u_tailFadeA >= u_tailFadeB)
                         ? 1.0
                         : 1.0 - smoothstep(u_tailFadeA, u_tailFadeB, vSegUV.y);

    float alpha = wisp * dissolveMask * vColor.a * tailMask;
    if (alpha < 0.003)
        discard;
    vec3 colour = vColor.rgb * wisp * dissolveMask * tailMask;
    finalColor = vec4(colour, alpha);
}
