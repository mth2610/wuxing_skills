#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"

// Black-hole "swirl" shader — painted directly onto a sphere's own UV
// surface (never a separate flat plane, which used to cut straight through
// the event-horizon mesh). Technique: convert the sphere's own longitude/
// latitude UV into a polar (angle, radius) pair, spin the angle over time
// and warp it by radius (angle += radius*K), sample FBM in that warped
// domain, then exponentially fade density away from the equator band so
// the glow reads as a swirling ring wrapping the sphere rather than an
// evenly-lit ball. Reused multiple times at increasing radii (see
// vc_black_hole.inl) as a cheap "fake volume" stack instead of true
// raymarching — same idea, much less GPU cost.
//
// fragTexCoord.x = longitude (0..1, wraps around the vertical axis)
// fragTexCoord.y = latitude  (0..1, pole to pole)

uniform vec4  u_bodyColor;
uniform vec4  u_glowColor;
uniform float u_opacity;
uniform float u_swirlSpeed;  // sign flips spin direction — counter-rotate shells for turbulence
uniform float u_noiseScale;
uniform float u_bandWidth;   // 0..1, how tightly density concentrates around the equator band

float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i);
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm3(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 4; k++) {
        v += a * vnoise3(p);
        p  = p * 2.15 + vec3(9.1, 3.7, 6.3);
        a *= 0.5;
    }
    return v;
}

void main() {
    float angle = fragTexCoord.x * 6.2831853; // 0..2*PI
    float r     = abs(fragTexCoord.y - 0.5) * 2.0; // 0 at equator .. 1 at poles

    // Swirl: angle warps by radius (spiral wind-up toward the poles), then
    // spins over time — exactly the "angle += radius*K; angle -= time*speed"
    // recipe. Sign of u_swirlSpeed picks spin direction (per-shell counter-
    // rotation is what sells "turbulent", not just "spinning").
    angle += r * 10.0;
    angle -= u_time * u_swirlSpeed;

    vec3 dom = vec3(cos(angle), sin(angle), r * 2.2) * u_noiseScale;
    float n1 = fbm3(dom + vec3(0.0, 0.0, u_time * 0.12));
    float n2 = fbm3(dom * 1.8 + vec3(u_time * 0.2, 0.0, 0.0));

    float ridge = 1.0 - abs(2.0 * n1 - 1.0);
    float wisp  = ridge * (0.35 + 0.65 * n2);

    // Density concentrates at the equator band (r=0) and falls off toward
    // the poles — the literal "density *= exp(-radius*4.0)" step, tuned by
    // u_bandWidth so different concentric shells can read as a wider/
    // narrower ring.
    float density = wisp * exp(-r * (3.5 / max(u_bandWidth, 0.05)));
    density = smoothstep(0.12, 0.8, density);

    vec3 col   = mix(u_bodyColor.rgb, u_glowColor.rgb, density);
    float alpha = density * u_opacity * u_bodyColor.a;

    finalColor = vec4(col * 2.0, alpha);
}
