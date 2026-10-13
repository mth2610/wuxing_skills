#version 330
#include "core/shaders/common/vs_header.glsl"
#include "core/deform/shaders/mesh_deform.glsl"

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

// ── WebGL Deform Extensions ─────────────────────────────────────────────────
uniform vec2  u_roll;         // x = rollRate (rad/m), y = rollSpeed (rad/s)
uniform vec3  u_drift;        // directional drift acceleration (seg*seg)
uniform vec4  u_jitter;       // x = amp, y = freq, z = quantized time step freq, w = head clamp
uniform vec4  u_widthProfile; // x = taperPow, y = headLen, z = growAge, w = bulgeMid
uniform vec2  u_shard;        // x = shardFreq, y = shardDepth
uniform vec4  u_wave;         // x = amp, y = freq, z = speed, w = agePow
uniform vec4  u_noise;        // x = amp, y = freq, z = speed, w = agePow
uniform vec4  u_helix;        // x = radius, y = freq, z = speed, w = phase
uniform vec4  u_lane;         // x = offset, y = scale, z = freq, w = speed
uniform vec2  u_cap;          // x = headRound, y = tailEase
uniform float u_seed;
uniform vec2  u_pathArc;      // x = laid odometer (m), y = visible trail length (m)

out float vPathMetres;
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
    vPathMetres = 0.0;
    float sideLen = length(vertexNormal);
    vec3 side = (sideLen > 0.5) ? normalize(vertexNormal) : vec3(1.0, 0.0, 0.0);
    float halfWidth = max(sideLen - 1.0, 0.0);
    float seg = clamp(vertexTexCoord.y, 0.0, 1.0);
    float vDist = seg * max(u_pathArc.y, 1.0);

    float headFade = max(u_waveEnv.x, 0.001);
    float tailStart = clamp(u_waveEnv.y, u_waveEnv.x, 0.999);
    float env = smoothstep(0.0, headFade, seg) *
                (1.0 - smoothstep(tailStart, 1.0, seg));

    float uAcross = 1.0 - 2.0 * vertexTexCoord.x;
    vec3 c = vertexPosition - side * (halfWidth * uAcross);
    float t = u_time;
    float phase = u_wavePhase;

    // ── Tangent & Bi-normal for ribbon frame ────────────────────────────────
    vec3 T = normalize(cross(side, u_stripNormal));
    if (dot(T, T) < 0.01) T = vec3(0.0, 0.0, 1.0);
    vec3 B = u_stripNormal;

    // ── ROLL: twist ribbon frame around tangent axis (screw / twist) ────────
    vec3 sd = side;
    if (u_roll.x != 0.0 || u_roll.y != 0.0)
    {
        float th = vDist * u_roll.x - t * u_roll.y;
        sd = sd * cos(th) + cross(T, sd) * sin(th);
    }

    // ── WAVE: sin dọc theo path, biên độ tăng theo tuổi (from trail.html) ──
    if (u_wave.x > 0.0)
    {
        float wv = sin(vDist * u_wave.y - t * u_wave.z + u_seed * 6.2831853);
        float wa = u_wave.x * pow(seg, max(u_wave.w, 0.01));
        c += u_stripNormal * wv * wa;
        c += B * cos(vDist * u_wave.y * 0.7 - t * u_wave.z * 1.3 + u_seed * 4.0) * wa * 0.5;
    }

    // ── NOISE: turbulence 3D trong world space (from trail.html) ────────────
    if (u_noise.x > 0.0)
    {
        vec3 q = c * u_noise.y + vec3(0.0, -t * u_noise.z, 0.0) + u_seed * 13.7;
        c += (vec3(VNoise3D(q), VNoise3D(q + vec3(17.1)), VNoise3D(q + vec3(31.7))) - 0.5) * 2.0 * u_noise.x * pow(seg, max(u_noise.w, 0.01));
    }

    // ── DRIFT: directional acceleration by age^2 (rising fire / billowing smoke)
    if (dot(u_drift, u_drift) > 0.0001)
    {
        c += u_drift * (seg * seg);
    }

    // ── HELIX: quấn quanh path (Linh Toàn / Thái Cực, from trail.html) ───────
    if (u_helix.x > 0.0)
    {
        float ph = vDist * u_helix.y - t * u_helix.z + u_helix.w;
        c += (u_stripNormal * cos(ph) + B * sin(ph)) * u_helix.x * smoothstep(0.0, 0.4, vDist) * (1.0 - 0.35 * seg);
    }

    // ── JITTER: multi-octave quantized zigzag (lightning arc) ────────────────
    if (u_jitter.x > 0.0)
    {
        float tq = floor(t * u_jitter.z) * 13.37 + u_seed * 91.7;
        float x = vDist * u_jitter.y;
        float amp = 1.0;
        vec2 o = vec2(0.0);
        for (int i = 0; i < 4; i++)
        {
            float fi = float(i);
            float i0 = floor(x + fi * 7.3);
            float h0 = fract(sin(i0 + tq + fi * 3.1) * 43758.5453);
            float h1 = fract(sin(i0 + 1.0 + tq + fi * 3.1) * 43758.5453);
            float z0 = mix(h0, h1, fract(x + fi * 7.3));

            float i1_idx = floor(x + fi * 3.7);
            float h2 = fract(sin(i1_idx + tq + fi * 5.9 + 11.0) * 43758.5453);
            float h3 = fract(sin(i1_idx + 1.0 + tq + fi * 5.9 + 11.0) * 43758.5453);
            float z1 = mix(h2, h3, fract(x + fi * 3.7));

            o += (vec2(z0, z1) - 0.5) * amp;
            x *= 2.1;
            amp *= 0.55;
        }
        c += (u_stripNormal * o.x + B * o.y) * 2.0 * u_jitter.x * smoothstep(0.0, u_jitter.w, vDist) * (1.0 - 0.3 * seg);
    }

    // ── LANE: bundle nhiều sợi lệch khỏi trục, xoáy theo d (Phong Nhận) ─────
    if (u_lane.x != 0.0)
    {
        float th = vDist * u_lane.z - t * u_lane.w;
        vec3 ax = sd * cos(th) + cross(T, sd) * sin(th);
        c += ax * u_lane.x * u_lane.y * (0.2 + 1.2 * seg);
    }

    // ── WIDTH PROFILING & SHARD SERRATIONS ──────────────────────────────────
    float hw = 1.0;
    if (u_widthProfile.x > 0.001 || u_widthProfile.y > 0.001 || u_widthProfile.z > 0.001 || u_widthProfile.w > 0.001 || u_cap.x > 0.0 || u_cap.y > 0.0)
    {
        float taper = pow(clamp(1.0 - seg, 0.0, 1.0), max(u_widthProfile.x, 0.01)) * mix(1.0, smoothstep(0.0, 0.4, 1.0 - seg), u_cap.y);
        float headLen = max(u_widthProfile.y, 0.0001);
        float ht = clamp(vDist / headLen, 0.0, 1.0);
        float head = (u_widthProfile.y > 0.0) ? mix(sin(ht * 1.5707963), sqrt(max(1.0 - (1.0 - ht) * (1.0 - ht), 0.0)), u_cap.x) : 1.0;
        float bulge = 1.0 + u_widthProfile.w * sin(3.14159265 * clamp(seg, 0.0, 1.0));
        float grow = 1.0 + u_widthProfile.z * seg;
        float shard = 1.0;
        if (u_shard.y > 0.0)
        {
            shard = mix(1.0, 0.2 + 0.8 * fract(vDist * u_shard.x), u_shard.y);
        }
        hw = taper * head * bulge * grow * shard;
    }

    vec3 pos = c + sd * (halfWidth * hw * uAcross);

    if (u_deformMode >= 3.5)
    {
        // NOISE — organic non-periodic wobble; side + a slice of stripNormal.
        float n = VNoise3D(pos * u_curlScale + vec3(t * 0.21, t * 0.13, 0.0));
        pos += (sd + u_stripNormal * 0.6) * (n - 0.5) * u_waveStrength * env * 2.0;
    }
    else if (u_deformMode >= 2.5)
    {
        // HELIX — corkscrew in the strip's own (side, stripNormal) frame.
        float a = seg * u_waveFreq.x * TAU + t * u_waveSpeed.x + phase;
        pos += (sd * cos(a) + u_stripNormal * sin(a)) * u_waveAmpA.x * u_waveStrength * env;
    }
    else if (u_deformMode >= 1.5)
    {
        // CURL3 — true 3D swirl from hash noise, projected onto the strip frame.
        vec3 n = vec3(VNoise3D(pos * u_curlScale + vec3(t * 0.31, t * 0.17, t * 0.23)),
                      VNoise3D(pos * u_curlScale + vec3(t * 0.17, t * 0.23, t * 0.31)),
                      VNoise3D(pos * u_curlScale + vec3(t * 0.23, t * 0.31, t * 0.17)));
        vec3 d = sd * (n.x - 0.5) + u_stripNormal * (n.y - 0.5);
        pos += d * u_waveStrength * env * 2.0;
    }
    else if (u_deformMode >= 0.5)
    {
        vec3 d = MeshDeform_ApplyField(vec2(seg, seg), vec2(seg, seg), t,
                                       side, u_stripNormal, 3, 3);
        pos += d * env * u_waveStrength;
    }

    vColor = vertexColor;
    vSegUV = vec2(vertexTexCoord.x, seg);
    VS_FinalOutput(pos);
}
