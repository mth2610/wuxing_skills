#version 330
#include "core/shaders/common/fs_header.glsl"
// Volume campfire — raymarched fire volume inside the bounding proxy sphere (Vulkan stress test:
// heavy fbm + domain warp + up-to-104-step march per fragment). Local-space reconstruction from
// fragNormal (robust, no matModel/world-space dependency — same approach as volume_smoke), emission
// /absorption front-to-back, blackbody temperature ramp, upward-advected turbulence with domain warp.
// fragPosition / fragNormal / viewPos / u_time come from fs_header.glsl (auto-bound by the engine).

uniform float u_radius;   // bounding-sphere radius (= the DrawCoreSphere radius); fire base at q.y=-1

// ---- value noise + fbm -------------------------------------------------------
float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float vnoise(vec3 x)
{
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p *= 2.02; a *= 0.5; }
    return s;
}

// fire density in [0,1] at a point in unit-sphere space q (up = +y, flame base at q.y = -1)
float density(vec3 q, out float heat)
{
    heat = 0.0;
    float ny = clamp((q.y + 1.0) / 1.9, 0.0, 1.0);   // 0 at base -> 1 near the top
    if (ny <= 0.0 || ny >= 1.0) return 0.0;
    float r = length(q.xz);

    float radius = mix(0.72, 0.04, pow(ny, 0.72));    // wide base tapering to a tip

    // rising, domain-warped turbulence (elongated vertically -> tall licking tongues)
    vec3 sp = q * vec3(3.0, 1.4, 3.0);
    sp.y -= u_time * 2.2;
    vec3 warp = vec3(fbm(sp * 0.8 + 11.5), fbm(sp * 0.8 + 27.3), fbm(sp * 0.8 + 3.1)) - 0.5;
    float n = fbm(sp + warp * 1.5);
    n += 0.5 * fbm(sp * 2.8 - vec3(0.0, u_time * 3.4, 0.0));   // finer flicker detail
    n /= 1.5;

    float streak = fbm(vec3(sp.x * 1.6, sp.y * 0.5, sp.z * 1.6) + 5.0);   // vertical tongue streaks
    float edge = radius * (0.5 + 1.05 * n);                    // noisy outline -> tongues + gaps
    float d = smoothstep(edge, edge * 0.12, r) * (0.2 + 1.0 * n) * (0.55 + 0.8 * streak);
    d *= smoothstep(1.0, 0.28, ny);                            // fade + break the tips into wisps
    d *= smoothstep(0.0, 0.06, ny);                            // fade right at the base
    d = clamp(d * 2.4 - 0.28, 0.0, 1.0);

    heat = clamp((1.0 - ny * 0.9) * (0.5 + 0.75 * d) + 0.16 * n, 0.0, 1.0);
    return d;
}

// blackbody-ish fire ramp, cold(dark red) -> hot(white)
vec3 fireColor(float t)
{
    vec3 c = mix(vec3(0.35, 0.02, 0.0), vec3(1.0, 0.28, 0.02), smoothstep(0.0, 0.35, t));
    c = mix(c, vec3(1.0, 0.72, 0.16), smoothstep(0.35, 0.68, t));
    c = mix(c, vec3(1.0, 0.96, 0.82), smoothstep(0.68, 1.0, t));
    return c;
}

void main()
{
    float R = max(u_radius, 0.3);
    vec3 rd = normalize(fragPosition - viewPos);
    vec3 p = normalize(fragNormal) * R;   // start at the sphere's front surface (center = origin, local)
    p += rd * (R * 0.02);                 // nudge just inside the shell
    float dt = R * 0.03;

    vec3 accum = vec3(0.0);
    float trans = 1.0;
    for (int i = 0; i < 104; i++)
    {
        if (dot(p, p) > R * R) break;     // exited the bounding sphere
        vec3 q = p / R;                   // unit-sphere space
        float heat;
        float d = density(q, heat);
        if (d > 0.001)
        {
            vec3 col = fireColor(heat) * (0.7 + 1.8 * d);
            float a = d * 0.7;
            accum += trans * col * a;
            trans *= (1.0 - a);
            if (trans < 0.02) break;
        }
        p += rd * dt;
    }

    float alpha = 1.0 - trans;
    if (alpha < 0.01) discard;
    finalColor = vec4(accum, alpha);      // drawn under BLEND_ADDITIVE by the skill
}
