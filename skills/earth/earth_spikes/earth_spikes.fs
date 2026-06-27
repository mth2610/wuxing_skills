/*
 * earth_spikes.fs  —  Địa Sa Châm dissolve / erosion shader
 * GLSL version : 330 core  (OpenGL 3.3 — matches project spec)
 *
 * Place in: skills/earth/earth_spikes/earth_spikes.fs
 *
 * How it works
 * ============
 * When a spike's hold-timer expires, u_dissolve ramps 0 → 1 over
 * SPIKE_DISSOLVE_TIME seconds.  The shader discards fragments so the
 * spike appears to crumble from its TIP downward, sinking back into
 * the earth.  An ember-orange glow traces the erosion boundary.
 *
 * UV assumption
 * =============
 * Raylib's DrawCylinder / GenMeshCylinder maps:
 *   fragTexCoord.y = 0 at the BOTTOM ring
 *   fragTexCoord.y = 1 at the TOP  ring
 * The dissolve therefore erodes fragments with high V first.
 *
 * Uniforms (set by earth_spikes_skill.c)
 * =======================================
 *   u_dissolve  – normalised dissolve amount  [0.0 intact → 1.0 gone]
 *   u_time      – game time in seconds (for subtle animation)
 */

#version 330

/* ── Raylib standard varyings ──────────────────────────────── */
in vec2 fragTexCoord;
in vec4 fragColor;

/* ── Raylib standard uniforms ──────────────────────────────── */
uniform sampler2D texture0;
uniform vec4      colDiffuse;

/* ── Custom uniforms ───────────────────────────────────────── */
uniform float u_dissolve;   /* 0.0 = fully intact, 1.0 = fully eroded  */
uniform float u_time;       /* game time in seconds                     */

out vec4 finalColor;

/* ================================================================
 * Noise helpers
 * ================================================================
 * valueNoise returns a smooth [0, 1] value.
 * fbm layers two octaves for a rougher, rock-like dissolution edge
 * without the cost of a texture lookup.
 * ================================================================ */

float hash21(vec2 p)
{
    p  = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 17.31);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    /* Smoothstep (C1) for the interpolation kernel */
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x),
               mix(c, d, u.x),
               u.y);
}

float fbm(vec2 p)
{
    /*
     * Two octaves:
     *   coarse (0.60) — defines the large erosion shapes
     *   fine   (0.30) — adds crackle detail to the edge
     * A tiny high-frequency flicker (0.10) animates the boundary.
     */
    float v = 0.0;
    v += 0.60 * valueNoise(p);
    v += 0.30 * valueNoise(p * 2.1  + vec2(3.70, 1.20));
    v += 0.10 * valueNoise(p * 4.3  + vec2(u_time * 0.9, 7.40));
    return v;   /* range ≈ [0, 1] */
}

/* ================================================================
 * Main
 * ================================================================ */
void main()
{
    vec4 texSample = texture(texture0, fragTexCoord);
    vec4 baseColor = texSample * colDiffuse * fragColor;

    /* ── Fast-path: no erosion when dissolve is essentially zero ── */
    if (u_dissolve < 0.001)
    {
        /* Apply only the depth-darkening term and exit early */
        float depth    = mix(0.72, 1.0, fragTexCoord.y);
        finalColor     = vec4(baseColor.rgb * depth, baseColor.a);
        return;
    }

    /* ── Dissolve mask ─────────────────────────────────────────────
     *
     * "heightFromTop" goes from 0 at the spike's TIP (V = 1) to 1
     * at the BASE (V = 0).  The erosion front descends from tip to
     * base as u_dissolve increases.
     *
     * "front" is the position of the dissolve wavefront at this UV:
     *   front > noise  →  the front has overtaken this fragment → discard
     *   front ≤ noise  →  the front has not yet arrived         → keep
     *
     * The (1 - u_dissolve * 0.4) multiplier slows the front near
     * the base so the very last remnant of the spike clings to the
     * ground until u_dissolve is nearly 1, which looks more natural.
     * ─────────────────────────────────────────────────────────────*/

    float heightFromTop = 1.0 - fragTexCoord.y;

    /* Animate noise very slightly so the dissolve boundary shimmers */
    float noise = fbm(fragTexCoord * 5.5 + vec2(0.0, u_time * 0.15));

    /* Erosion front descends as dissolve grows */
    float front     = u_dissolve
                    - heightFromTop * (1.0 - u_dissolve * 0.4);

    /* threshold < 0 → the front has passed → discard the fragment  */
    float threshold = noise - front;

    if (threshold < 0.0) {
        discard;
    }

    /* ── Ember glow at the erosion boundary ───────────────────────
     *
     * "threshold" is the signed distance above the dissolve front.
     * Fragments with threshold ≈ 0 are right at the crumble edge
     * and receive the brightest glow; it fades exponentially further
     * from the boundary.
     *
     * The colour shifts from warm amber at the start of the dissolve
     * to a deeper ember-red as the spike is nearly gone.
     * ─────────────────────────────────────────────────────────────*/

    float edgeGlow  = exp(-threshold * 8.0) * u_dissolve;

    vec3 glowColor  = mix(vec3(1.00, 0.60, 0.08),   /* amber at start */
                          vec3(0.90, 0.18, 0.02),    /* deep red at end */
                          u_dissolve);

    baseColor.rgb  += glowColor * edgeGlow * 2.4;

    /* ── Depth shading: base of spike is slightly darker ────────── */
    float depth     = mix(0.72, 1.0, fragTexCoord.y);
    baseColor.rgb  *= depth;

    finalColor = baseColor;
}
