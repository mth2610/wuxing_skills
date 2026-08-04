#version 330
#include "core/shaders/common/fs_header.glsl"

// ── TRAIL VOLUME — opacity for a swept tube that has to read as gas ─────────
//
// The geometry stage can only ever produce a lumpy SOLID. What separates a
// bent cylinder from smoke happens here, and it is three multiplies:
//
//     opacity = pattern * fade * edge * density
//
// MULTIPLIED, not layered. Two sheets composited alpha-over can only ever add
// coverage, so a second pass makes the body MORE opaque and the result reads
// as polished stone — which is exactly how this column shipped. A product of
// two masks is sparser than either factor, so the same two samples carve holes
// instead of filling them. That is the whole difference.
//
// SPACE. fragPosition/fragNormal come through vs_header's VS_FinalOutput, and
// every fresnel that ships in this engine — plasma_shell, crystal, aura_shell,
// effect_material, water_splash — pairs them with `viewPos` exactly like this.
//
// This was changed to a view-space reading mid-session and changed back: the
// evidence for the change came from debug views that were sitting BELOW two
// discards and therefore only ever painted the fragments those discards let
// through. Nothing measured through them can be trusted, so the engine's own
// convention stands until something better than a contaminated reading says
// otherwise.

in vec4 vColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

// .x/.z = ALONG-body pan of sheet 1 / sheet 2 (tiles per second).
// .y/.w = AROUND-the-body pan. KEEP THESE AT ZERO unless a swirl is wanted.
//
// Panning in u moves the texture around the circumference — that is literally
// rotation, and combined with any along-pan the net motion is a diagonal, i.e.
// a screw thread climbing the body. The first draft of this file panned both
// axes and manufactured exactly the tornado this effect was being fixed for.
// Smoke rises; it does not spin.
uniform vec4 u_volPan;
// .x = ALONG tiling of the second sheet. Its AROUND tiling is fixed at 1 wrap
//      below and is not exposed.
// .y = depth power, .z = silhouette softness, .w = master density.
uniform vec4 u_volMask;
// DEBUG VIEW — 0 = off. Paints one intermediate quantity as opaque greyscale
// so a screenshot answers what the term actually does across the column,
// instead of another round of guessing at its constants.
//   1 = the thickness term `edge`      2 = |N.V| raw
//   3 = the sheet product `pattern`    4 = the vertex fade
//   5 = length(fragNormal): WHITE = the vertex attribute arrives, BLACK = it
//       does not. No normalize, no dot, so it cannot produce NaN.
//   6 = length of the derivative normal before normalising, x8: BLACK = the
//       triangle is degenerate in screen space and cross() collapsed.
//   7 = |fragPosition| / 40 — WORLD space or VIEW space, decisively
//   8 = constant mid grey, 9 = constant red. Nothing computed, nothing
//       interpolated: if these do not arrive as written, no other reading from
//       this shader means anything. See sandbox/colour_probe.c.
//   5 = fragNormal as colour           6 = the view vector V as colour
//   7 = |fragPosition| / 40, greyscale — WORLD or VIEW space, decisively
uniform float u_volDebug;

void main()
{
    // THE CONSTANT DEBUG MODES COME FIRST, ABOVE EVERYTHING.
    //
    // They used to sit near the bottom, below two `discard`s — so a fragment
    // that the shader was going to throw away never reached them. The colour
    // probe caught it: mode 8 asks for flat grey and the framebuffer came back
    // with the background, because the fragment was discarded before the branch
    // was ever evaluated. Every debug image taken this session therefore showed
    // only the fragments that SURVIVED the discards, not the surface.
    //
    // A debug view has to short-circuit the shader, not live inside it.
    if (u_volDebug > 7.5 && u_volDebug < 8.5) {
        finalColor = vec4(0.502, 0.502, 0.502, 1.0);
        return;
    }
    if (u_volDebug > 8.5) {
        finalColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }
    // fract() the pan and nothing else. u_time is unbounded, and a noise
    // domain fed an unbounded value degenerates into flat blocks after a long
    // session (ENGINE_LANDMINES §8). The fold is EXACT here and only here: the
    // sheet tiles on both axes (assets/TEXTURE_PACKING.md), so the sampler
    // repeats with period 1 and dropping whole periods cannot change a texel.
    // Never fold the sum as well — fract(fract(x) * k) is not fract(x * k).
    vec2 pan1 = fract(vec2(u_volPan.y, u_volPan.x) * u_time);
    vec2 pan2 = fract(vec2(u_volPan.w, u_volPan.z) * u_time);

    // Sheet 2 wraps ONCE around too — same as sheet 1, deliberately.
    //
    // It used to scale u by 2, which doubles the tongue count for that layer:
    // 4 tongues around became 8, and since only half a cylinder faces the
    // camera the render showed 4 where the reference shows 2. The two layers
    // are decorrelated by their ALONG scale and their pan instead, which leaves
    // the tongues lined up in u and lets the product make them breathe — which
    // is what the reference's two strands actually do.
    vec2 uv2 = vec2(fragTexCoord.x, fragTexCoord.y * u_volMask.x);

    vec4 s1 = texture(texture0, fragTexCoord + pan1);
    vec4 s2 = texture(texture0, uv2 + pan2);

    // A = coverage in the OPAQUE layout; RGB is grey luminance kept grey so
    // the caller's tint survives.
    float pattern = s1.a * s2.a;

    // EDGE — an optical-depth term times a silhouette rolloff.
    //
    // This is the state the effect was in when it last read as "close, only the
    // edges are hard". Everything tried after that point was justified by debug
    // images that turned out to be measuring a discard rather than a surface,
    // so it has all been reverted rather than kept on a guess.
    //
    // What core/tests/silhouette_test.c established independently of any of
    // those images still stands and is written up there: a per-fragment term
    // cannot dissolve a boundary while a grazing ray crosses an unbounded
    // number of facets, and the power has to be at least 2 because |N.V| leaves
    // the silhouette with an infinite derivative. Applying either of those
    // needs a reading this session has not honestly obtained yet.
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    float d = abs(dot(N, V));
    float depth = pow(clamp(1.0 - d, 0.0, 1.0), max(u_volMask.y, 0.001));
    float rim = smoothstep(0.0, max(u_volMask.z, 0.001), d);
    float edge = depth * rim;

    // The vertical fade arrives as vertex alpha (PMTube_DrawFaded), already
    // multiplied by the layer's own alpha.
    float fade = vColor.a * colDiffuse.a;

    // Painted BEFORE the alpha gate, and fully opaque, so a term that is
    // silently zero still shows as black rather than as nothing at all — the
    // two are the same picture otherwise.
    if (u_volDebug > 0.5) {
        // Directions as colour, so a wrong one is READABLE rather than merely
        // wrong: on a cylinder a correct radial normal sweeps the full hue
        // circle around the body, and a correct V barely changes across it.
        if (u_volDebug > 4.5 && u_volDebug < 5.5) {
            finalColor = vec4(N * 0.5 + 0.5, 1.0); return;
        }
        if (u_volDebug > 5.5 && u_volDebug < 6.5) {
            finalColor = vec4(V * 0.5 + 0.5, 1.0); return;
        }
        // THE DECISIVE ONE. fragPosition comes through matModel, and whether
        // that matrix is identity or model x view (ENGINE_LANDMINES §9) decides
        // whether a world-space camera minus this position means anything at
        // all. The two cases
        // differ by a MAGNITUDE, not by a pattern: the column stands near the
        // arena centre (6, 0, 4.4), so |P| is about 7 in world space and about
        // the camera distance — tens of metres — in view space. Scaled by 40 a
        // world-space read is dark grey and a view-space read is near white.
        //
        // A debug view that cannot tell the failure from success is worse than
        // none (core/CLAUDE.md §6), which is exactly why this paints a distance
        // to a known point and not fract() of a position.
        if (u_volDebug > 6.5) {
            float q7 = clamp(length(fragPosition) / 40.0, 0.0, 1.0);
            finalColor = vec4(q7, q7, q7, 1.0); return;
        }
        // MODES 8 / 9 — the instrument check, and it comes FIRST because
        // every other mode is only meaningful if this one passes. Grey has
        // R == G == B so any channel swap or per-channel curve shows; red is
        // one channel so a swizzle is unmistakable.
        // MODE 5 — is the vertex normal attribute actually arriving? Paints
        // |dot(attribute, derivative)|: 1 where they agree, 0 where they do
        // not. The effect no longer depends on the answer, but the answer is
        // worth having: a silently-absent attribute would explain several
        // other shaders too.
        if (u_volDebug > 4.5 && u_volDebug < 5.5) {
            // Raw magnitude. If the attribute never arrives this is 0 and the
            // column is black; if it arrives it is ~1 and the column is white.
            // No normalize and no dot, so NaN is impossible and the answer
            // cannot be confused with a numerical accident.
            float q5 = clamp(length(fragNormal), 0.0, 1.0);
            finalColor = vec4(q5, q5, q5, 1.0);
            return;
        }
        if (u_volDebug > 5.5 && u_volDebug < 6.5) {
            float q6 = clamp(ngLen * 8.0, 0.0, 1.0);
            finalColor = vec4(q6, q6, q6, 1.0);
            return;
        }
        // MODE 7 — the one that settles the space question by MAGNITUDE
        // rather than by pattern. The column stands near the arena centre
        // (6, 0, 4.4), so |fragPosition| is about 7 m if matModel left it in
        // world space, and the camera distance — tens of metres — if matModel
        // was model x view. Divided by 40: world reads dark grey, view reads
        // near white, and the two cannot be confused.
        //
        // ENGINE_LANDMINES §9 records that fract(fragPosition) was once used
        // for this and painted a clean grid either way. A debug view that
        // cannot tell the failure from success is worse than none.
        if (u_volDebug > 6.5) {
            float q7 = clamp(length(fragPosition) / 40.0, 0.0, 1.0);
            finalColor = vec4(q7, q7, q7, 1.0);
            return;
        }
        float q = (u_volDebug < 1.5) ? edge
                : (u_volDebug < 2.5) ? abs(facing)
                : (u_volDebug < 3.5) ? pattern
                                     : fade;
        finalColor = vec4(q, q, q, 1.0);
        return;
    }

    float alpha = pattern * fade * edge * u_volMask.w;
    if (u_volDebug < 0.5 && alpha < 0.003) discard;

    vec3 colour = s1.rgb * vColor.rgb * colDiffuse.rgb;
    finalColor = vec4(colour, alpha);
}
