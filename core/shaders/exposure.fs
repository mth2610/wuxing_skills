#version 330

// AUTO EXPOSURE — one fragment, one pass, no CPU readback.
//
// Meters the BACKGROUND luma target (SceneTargets_CaptureBackgroundLuma), which
// is captured after the world and BEFORE any VFX. That choice does the work of
// §7.5's "no spell-caused exposure oscillation" by construction rather than by
// tuning: a spell is not in the image being metered, so it cannot drive the
// exposure that is about to be applied to it.
//
// GEOMETRIC mean, not arithmetic: exposure is a multiplicative quantity, and an
// arithmetic mean lets one small blazing region drag the whole frame dark.
//
// EXPOSURE IS CLAMPED TO <= 1.0, so this can only ever DARKEN. That is what makes
// it safe to ship into a night arena: the night scene meters at ~0.02, wants a
// large exposure, is clamped to 1.0, and comes out bit-identical. Only a scene
// bright enough to need exposing DOWN is affected at all.

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
    // A fixed grid rather than every texel: 1/16-scale luma is already an
    // average, and a grid keeps the cost independent of resolution.
    const int N = 12;
    float logSum = 0.0;
    float wSum = 0.0;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / float(N);
            // DOWN-WEIGHT THE EDGES (§7.5). What is at the frame's rim is
            // usually not what the player is looking at, and letting it swing
            // the exposure makes the image pump as the camera turns.
            vec2 d = abs(uv - 0.5) * 2.0;
            float w = (1.0 - smoothstep(0.55, 1.0, max(d.x, d.y))) * 0.9 + 0.1;
            float l = texture(u_lumaTex, uv).r;
            logSum += log(max(l, 1e-4)) * w;
            wSum += w;
        }
    }
    float avg = exp(logSum / max(wSum, 1e-4));

    float target = clamp(u_targetGrey / max(avg, 1e-4), u_minExposure, 1.0);
    float prev = texture(texture0, vec2(0.5)).r;
    if (prev <= 0.0) prev = 1.0;                 // first frame: start unexposed

    // Two speeds (§7.5): the eye closes down fast and opens slowly, and a fast
    // brightening is the case where a wrong exposure is most obvious.
    float rate = (target < prev) ? u_speedDown : u_speedUp;
    float k = 1.0 - exp(-max(rate, 0.0) * max(u_dt, 0.0));
    float e = mix(prev, target, clamp(k, 0.0, 1.0));

    finalColor = vec4(e, e, e, 1.0);
}
