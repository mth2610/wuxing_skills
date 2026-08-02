#version 330
#include "core/shaders/common/vs_header.glsl"

// ── TRAIL DEFORM — uber vertex deformation for ribbon trails ────────────────
// One shader, five deform modes selected by a uniform (a uniform branch costs
// a few ALU and lets every trail share one program — no per-mode shader, no
// extra state switches):
//
//   u_deformMode  1 = sin-multi (2-3 sin octaves — fire/energy wisp)
//                 2 = curl3     (hash-noise 3D swirl — real depth, not 2D)
//                 3 = helix     (corkscrew around the strip's (side, normal) frame)
//                 4 = noise     (organic non-periodic wobble)
//                 0 = passthrough (reserved; mode 0 trails never use this shader)
//
// The branch thresholds sit at 0.5/1.5/2.5/3.5 so INTEGER uniform modes 1..4
// land in exactly the branch above — a 1.5-based chain would send mode 1 into
// passthrough and shift every other mode up by one (this happened once).
//
// REINTERPRETED ATTRIBUTE: `vertexNormal` carries the ribbon's unit SIDE
// vector (across the strip width), written by DrawRibbonStripDeformedEx. A
// degenerate vector (e.g. a non-ribbon quad drawn under this shader, which
// never writes normals) falls back to a fixed axis instead of producing NaN.
//
// Coordinate input: vertexTexCoord.x = across (0..1), .y = path segment,
// 0 = head .. 1 = tail. The amplitude envelope (u_waveEnv) forces the wave to
// 0 at the head so the strip never detaches from its emitter, and to 0 at the
// tail so the old end does not thrash.
//
// All displacement happens in the strip's own frame — `side` (across width,
// per-vertex) and u_stripNormal (strip plane normal: view direction for
// camera-facing ribbons, world-up for RIBBON_WORLD_UP, fixedNormal for
// RIBBON_FIXED_NORMAL) — so the shape reads correctly from any camera angle.

in vec4 vertexColor;

uniform float u_deformMode;
uniform vec3  u_waveAmpA;    // per-octave amplitude along `side` (across width)
uniform vec3  u_waveAmpB;    // per-octave amplitude along u_stripNormal
uniform vec3  u_waveFreq;    // per-octave frequency (cycles along the path)
uniform vec3  u_waveSpeed;   // per-octave travel speed
uniform float u_wavePhase;   // per-spawn random phase — desyncs simultaneous casts
uniform vec2  u_waveEnv;     // x = head fade fraction, y = tail fade fraction
uniform float u_waveStrength;
uniform float u_curlScale;   // noise frequency for curl3 / noise modes
uniform vec3  u_stripNormal; // strip plane normal (viewDir / world-up / fixedNormal)

out vec4 vColor;
out vec2 vSegUV;             // x = across 0..1, y = segment 0 (head) .. 1 (tail)

const float TAU = 6.2831853;

float Hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float VNoise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i);
    float n100 = Hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = Hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = Hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = Hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = Hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = Hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = Hash13(i + vec3(1.0, 1.0, 1.0));
    float x00 = mix(n000, n100, f.x);
    float x10 = mix(n010, n110, f.x);
    float x01 = mix(n001, n101, f.x);
    float x11 = mix(n011, n111, f.x);
    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}

void main()
{
    float sideLen = length(vertexNormal);
    vec3 side = (sideLen > 0.5) ? normalize(vertexNormal) : vec3(1.0, 0.0, 0.0);
    float seg = clamp(vertexTexCoord.y, 0.0, 1.0);

    float headFade = max(u_waveEnv.x, 0.001);
    float tailStart = clamp(u_waveEnv.y, u_waveEnv.x, 0.999);
    float env = smoothstep(0.0, headFade, seg) *
                (1.0 - smoothstep(tailStart, 1.0, seg));

    vec3 pos = vertexPosition;
    float t = u_time;
    float phase = u_wavePhase;

    if (u_deformMode >= 3.5)
    {
        // NOISE — organic non-periodic wobble; side + a slice of stripNormal.
        float n = VNoise3D(pos * u_curlScale + vec3(t * 0.21, t * 0.13, 0.0));
        pos += (side + u_stripNormal * 0.6) * (n - 0.5) * u_waveStrength * env * 2.0;
    }
    else if (u_deformMode >= 2.5)
    {
        // HELIX — corkscrew in the strip's own (side, stripNormal) frame.
        float a = seg * u_waveFreq.x * TAU + t * u_waveSpeed.x + phase;
        pos += (side * cos(a) + u_stripNormal * sin(a)) * u_waveAmpA.x * u_waveStrength * env;
    }
    else if (u_deformMode >= 1.5)
    {
        // CURL3 — true 3D swirl from hash noise, projected onto the strip frame.
        vec3 n = vec3(VNoise3D(pos * u_curlScale + vec3(t * 0.31, t * 0.17, t * 0.23)),
                      VNoise3D(pos * u_curlScale + vec3(t * 0.17, t * 0.23, t * 0.31)),
                      VNoise3D(pos * u_curlScale + vec3(t * 0.23, t * 0.31, t * 0.17)));
        vec3 d = side * (n.x - 0.5) + u_stripNormal * (n.y - 0.5);
        pos += d * u_waveStrength * env * 2.0;
    }
    else if (u_deformMode >= 0.5)
    {
        // SIN_MULTI — the workhorse: 3 octaves along `side` + 3 along the strip
        // normal, each with its own frequency and travel speed, all phase-shifted
        // per spawn so no two casts look identical.
        vec3 d = vec3(0.0);
        d += side * (u_waveAmpA.x * sin(seg * u_waveFreq.x * TAU + t * u_waveSpeed.x + phase));
        d += side * (u_waveAmpA.y * sin(seg * u_waveFreq.y * TAU + t * u_waveSpeed.y + phase * 2.31));
        d += side * (u_waveAmpA.z * sin(seg * u_waveFreq.z * TAU + t * u_waveSpeed.z + phase * 4.67));
        d += u_stripNormal * (u_waveAmpB.x * sin(seg * u_waveFreq.x * 3.77 + t * u_waveSpeed.x * 1.31 + phase));
        d += u_stripNormal * (u_waveAmpB.y * sin(seg * u_waveFreq.y * 5.13 + t * u_waveSpeed.y * 0.63 + phase * 1.71));
        d += u_stripNormal * (u_waveAmpB.z * sin(seg * u_waveFreq.z * 2.89 + t * u_waveSpeed.z * 1.87 + phase * 3.13));
        pos += d * env * u_waveStrength;
    }

    vColor = vertexColor;
    vSegUV = vec2(vertexTexCoord.x, seg);
    VS_FinalOutput(pos);
}
