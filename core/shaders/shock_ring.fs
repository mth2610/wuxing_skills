#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform float u_opacity;
uniform float u_emission;
uniform float u_t01;
uniform float u_seed;
uniform float u_hole;
uniform float u_premultiply;
uniform int u_variant;
uniform vec2 u_arc;     // center and half span, in turns
uniform float u_direction;

// Polar value noise with an integer number of cells per turn. Wrap both
// lattice corners so the silhouettes close even when the arc crosses zero.
float ShockNoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    float x0 = mod(i.x, period);
    float x1 = mod(i.x + 1.0, period);
    return mix(mix(hash2(vec2(x0, i.y)), hash2(vec2(x1, i.y)), w.x),
               mix(hash2(vec2(x0, i.y + 1.0)), hash2(vec2(x1, i.y + 1.0)), w.x), w.y);
}

float Band(float distanceToCenter, float width, float pixel)
{
    // Preserve integrated brightness as a narrow ridge becomes subpixel.
    return (1.0 - smoothstep(0.0, width + pixel, abs(distanceToCenter))) *
           (width / (width + pixel * 0.5));
}

// Density controls coverage. Light models the lit face within that coverage.
// Heat feeds only the isolated emission pass.
struct Wave {
    float density;
    float light;
    float heat;
};

Wave Impact(float u, float x, float px, float angularPx, float macro)
{
    float t = u_t01;
    float lobes = ShockNoise(vec2(u * 13.0, u_seed + t * 0.72), 13.0);
    float lip = (lobes - 0.5) * 0.13 + (macro - 0.5) * 0.10;
    float depth = lip - x;
    float tail = max(depth, 0.0);
    float curl = tail * ((macro - 0.5) * 0.22 + tail * 0.31);
    float rolls = ShockNoise(vec2((u + curl) * 24.0, tail * 7.0 + u_seed - t * 1.3), 24.0);
    rolls = mix(rolls, 0.5, smoothstep(0.25, 0.68, angularPx * 24.0));
    float wisps = ShockNoise(vec2((u + curl * 0.64) * 58.0, tail * 6.0 + u_seed + 11.0), 58.0);
    wisps = mix(wisps, 0.5, smoothstep(0.24, 0.65, angularPx * 58.0));

    // Advect the broad lobes and let the narrow wisps trail from their shoulders.
    float crest = smoothstep(-0.055 - px, 0.028 + px, depth);
    float reach = mix(0.12, 0.33, lobes) + t * 0.062;
    float veil = 1.0 - smoothstep(reach * 0.14, reach, tail);
    float erosion = smoothstep(0.24, 0.76, rolls * 0.62 + wisps * 0.38 - t * 0.13);
    float compression = smoothstep(0.15, 0.76, macro * 0.50 + lobes * 0.50);
    float volume = crest * veil * (0.13 + compression * 0.64 + erosion * 0.43);
    float feather = crest * erosion * (1.0 - smoothstep(reach * 0.45, reach * 1.35, tail));
    float shoulder = Band(depth - mix(0.035, 0.092, rolls), mix(0.037, 0.096, lobes), px);
    float ignition = smoothstep(0.42, 0.76, lobes * 0.63 + wisps * 0.37);
    float flash = 1.0 - smoothstep(0.16, 0.78, t);
    float ridge = Band(depth - 0.019, 0.024, px) *
                  smoothstep(0.51, 0.80, rolls * 0.55 + lobes * 0.45);
    float heat = Band(depth - 0.035, 0.04, px) * ignition * compression;
    return Wave(volume + feather * 0.34,
                shoulder * compression * 0.86 + feather * 0.28 + ridge * 0.48,
                (heat * 0.84 + ridge * 0.78) * flash + shoulder * feather * 0.08);
}

Wave Parry(float u, float x, float px)
{
    float flash = 1.0 - smoothstep(0.20, 0.83, u_t01);
    float streak = ShockNoise(vec2(u * 5.0 - u_t01 * 0.96, u_seed + 21.0), 5.0);
    float sweep = smoothstep(0.27, 0.76, streak);
    float blade = Band(x, mix(0.014, 0.033, sweep), px);
    float bevel = Band(x + 0.024, 0.064, px);
    float echo = Band(x + mix(0.055, 0.22, u_t01), 0.022, px) * sweep * flash;
    return Wave(blade * 0.90 + bevel * 0.91 + echo * 0.35,
                blade * 0.84 + bevel * 0.15 + echo * 0.43,
                (blade * mix(0.36, 1.11, sweep) + echo * 0.49) * flash);
}

Wave Sonic(float x, float px, float macro)
{
    float compression = Band(x, 0.045, px);
    float trough = Band(x + 0.058, 0.073, px);
    float rebound = Band(x + mix(0.115, 0.257, u_t01), 0.085, px);
    float condensation = mix(0.37, 0.95, macro);
    return Wave(compression * 0.80 + trough * 0.72 + rebound * condensation * 0.57,
                compression * 0.84 + rebound * 0.53,
                compression * condensation * 0.45);
}

Wave Pulse(float u, float x, float px, float angularPx, float macro)
{
    float breadth = mix(0.13, 0.26, u_t01) * mix(0.79, 1.17, macro);
    float veil = Band(x + 0.047, breadth, px);
    float fold = ShockNoise(vec2(u * 11.0 + u_t01 * 0.23, u_seed + x * 7.0), 11.0);
    fold = mix(fold, 0.5, smoothstep(0.24, 0.70, angularPx * 11.0));
    float back = mix(0.12, 0.31, u_t01) + (macro - 0.5) * 0.135;
    float undercurrent = Band(x + back, mix(0.068, 0.107, fold), px);
    float sweep = smoothstep(0.20, 0.80, macro * 0.62 + fold * 0.38);
    float satin = Band(x - (fold - 0.5) * 0.073, mix(0.031, 0.065, sweep), px);
    float ripple = Band(x + back * 0.62 + (fold - 0.5) * 0.094, 0.022, px) * sweep;
    return Wave(veil * 0.54 + undercurrent * 0.37,
                satin * mix(0.25, 0.83, sweep) + undercurrent * 0.34 + ripple * 0.30,
                satin * veil * mix(0.20, 0.61, sweep));
}

Wave Concussive(float u, float x, float px, float angularPx, float macro)
{
    float t = u_t01;
    float crest = ShockNoise(vec2(u * 12.0, u_seed + t * 0.81), 12.0);
    float front = (macro - 0.5) * 0.16 + (crest - 0.5) * 0.14;
    float depth = front - x;
    float tail = max(depth, 0.0);
    float sweep = u + tail * (crest - 0.5) * 0.16;
    float billows = ShockNoise(vec2(sweep * 20.0, tail * 9.0 + u_seed - t * 2.1), 20.0);
    float grain = ShockNoise(vec2((sweep + tail * tail * 0.11) * 49.0,
                                 tail * 24.0 + u_seed - t * 2.4), 49.0);
    grain = mix(grain, 0.5, smoothstep(0.22, 0.61, angularPx * 49.0));
    float turbulence = billows * 0.68 + grain * 0.32;
    float reach = mix(0.17, 0.42, macro) + t * 0.045;
    float mass = smoothstep(-0.10 - px, 0.046 + px, depth) *
                 (1.0 - smoothstep(reach * 0.24, reach, tail));
    float breakup = smoothstep(0.21, 0.74, turbulence + crest * 0.21 - t * 0.10);
    float pressure = smoothstep(0.23, 0.72, mix(crest, macro, 0.47));
    float coverage = mass * (0.29 + breakup * 0.89 + pressure * 0.33);
    float shoulder = Band(depth - mix(0.06, 0.16, billows), 0.13, px);
    float litFold = shoulder * smoothstep(0.36, 0.84, turbulence) * (0.41 + pressure * 0.49);
    float flash = 1.0 - smoothstep(0.11, 0.53, t);
    return Wave(coverage, litFold + breakup * mass * 0.21, litFold * pressure * flash * 0.62);
}

void main()
{
    float u = fragTexCoord.x;
    float v = fragTexCoord.y;
    float px = clamp(fwidth(v) * 0.65, 0.001, 0.16);
    float angularPx = max(fwidth(u), 0.00005);
    float crossSection = u_direction < 0.0 ? 1.0 - v : v;
    float t = u_t01;
    float macro = ShockNoise(vec2(u * 7.0, u_seed), 7.0);
    float arcDistance = abs(fract(u - u_arc.x + 0.5) - 0.5);
    float tip = u_arc.y >= 0.5 ? 0.0 : smoothstep(u_arc.y * 0.58, u_arc.y, arcDistance);
    float arch = crossSection - 0.5 + tip * 0.085;
    float rip = ShockNoise(vec2(u * 19.0, u_seed + 31.0), 19.0) - 0.5;
    float x = arch - (macro - 0.5) * 0.059 - rip * 0.020;

    Wave wave;
    float fade = 1.0 - t;
    float vapour = 0.0;
    float hotWhite = 0.23;
    if (u_variant == 1) {
        wave = Parry(u, arch, px);
        fade *= mix(0.92, 0.44, t);
        hotWhite = 0.64;
    } else if (u_variant == 2) {
        wave = Sonic(arch - (macro - 0.5) * 0.012, px, macro);
        vapour = 0.80;
        hotWhite = 0.38;
    } else if (u_variant == 3) {
        wave = Pulse(u, x, px, angularPx, macro);
        fade = 1.0 - smoothstep(0.05, 1.0, t);
        hotWhite = 0.04;
    } else if (u_variant == 4) {
        wave = Concussive(u, x, px, angularPx, macro);
        fade = 1.0 - smoothstep(0.14, 1.0, t);
    } else {
        wave = Impact(u, x, px, angularPx, macro);
    }

    // Feather the actual mesh boundary before applying angular fades.
    float boundary = smoothstep(0.0, 0.05, v) * (1.0 - smoothstep(0.93, 1.0, v));
    boundary *= smoothstep(u_hole * 0.45, max(u_hole + 0.13, 0.04), crossSection);
    float envelope = boundary * (1.0 - tip) * fade * u_opacity * u_bodyColor.a;

    if (u_premultiply > 0.5) {
        vec3 hot = mix(u_glowColor.rgb, vec3(0.86, 0.95, 1.0), vapour);
        hot = mix(hot, vec3(1.0, 0.98, 0.94), hotWhite);
        vec3 glow = VFX_TonemapSafeEmission(hot, u_emission, wave.heat * envelope);
        finalColor = VFX_ResolvePremultiplied(vec3(0.0), 1.0, 0.0, glow, 1.0, 1.0);
    } else {
        vec3 pigment = mix(u_bodyColor.rgb, vec3(0.22, 0.34, 0.47), vapour);
        vec3 lit = mix(u_glowColor.rgb, vec3(0.78, 0.89, 0.98), vapour);
        vec3 body = mix(pigment * 0.30, lit * 0.83, clamp(wave.light, 0.0, 1.0));
        finalColor = VFX_ResolveBody(body, 1.0, wave.density * envelope);
    }
}
