#version 330
#include "core/shaders/common/vs_header.glsl"
#include "core/deform/shaders/mesh_deform.glsl"

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

// Vectorized 4-lane hash for 3D lattice corners
vec4 Hash43(vec4 px, vec4 py, float pz)
{
    vec4 x = fract(px * 0.1031);
    vec4 y = fract(py * 0.1031);
    vec4 z = vec4(fract(pz * 0.1031));
    vec4 d = x * (z + 31.32) + y * (y + 31.32) + z * (x + 31.32);
    x += d;
    y += d;
    z += d;
    return fract((x + y) * z);
}

float VNoise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    vec4 hx = vec4(i.x, i.x + 1.0, i.x, i.x + 1.0);
    vec4 hy = vec4(i.y, i.y, i.y + 1.0, i.y + 1.0);
    vec4 n0 = Hash43(hx, hy, i.z);
    vec4 n1 = Hash43(hx, hy, i.z + 1.0);
    vec4 nz = mix(n0, n1, f.z);
    return mix(mix(nz.x, nz.y, f.x), mix(nz.z, nz.w, f.x), f.y);
}

void main()
{
    float sideLenSq = dot(vertexNormal, vertexNormal);
    float sideLen = (sideLenSq > 0.25) ? sqrt(sideLenSq) : 0.0;
    vec3 side = (sideLen > 0.5) ? (vertexNormal / sideLen) : vec3(1.0, 0.0, 0.0);
    float halfWidth = max(sideLen - 1.0, 0.0);
    float seg = clamp(vertexTexCoord.y, 0.0, 1.0);
    float vDist = seg * max(u_pathArc.y, 1.0);
    float vArc = u_pathArc.x - vDist;
    vPathMetres = vArc;
    float headWeld = smoothstep(0.0, 0.16, seg);

    float headFade = max(u_waveEnv.x, 0.001);
    float tailStart = clamp(u_waveEnv.y, u_waveEnv.x, 0.999);
    float env = smoothstep(0.0, headFade, seg) *
                (1.0 - smoothstep(tailStart, 1.0, seg));

    float uAcross = 1.0 - 2.0 * vertexTexCoord.x;
    vec3 c = vertexPosition - side * (halfWidth * uAcross);
    float t = u_time;
    float phase = u_wavePhase;

    // ── Tangent & Bi-normal for ribbon frame ────────────────────────────────
    vec3 rawT = cross(side, u_stripNormal);
    float rawTLenSq = dot(rawT, rawT);
    vec3 T = (rawTLenSq > 1e-4) ? (rawT * inversesqrt(rawTLenSq)) : vec3(0.0, 0.0, 1.0);
    vec3 rawB = cross(T, side);
    float rawBLenSq = dot(rawB, rawB);
    vec3 B = (rawBLenSq > 1e-4) ? (rawB * inversesqrt(rawBLenSq)) : u_stripNormal;

    // ── ROLL: modulate ribbon width along stationary arc without out-of-plane twist spikes
    vec3 sd = side;
    if (u_roll.x != 0.0 || u_roll.y != 0.0)
    {
        float th = vArc * u_roll.x + t * u_roll.y;
        sd = side * (0.72 + 0.28 * cos(th));
    }

    // ── WAVE: sin dọc theo path (world-anchored), biên độ tăng theo tuổi ────
    if (u_wave.x > 0.0)
    {
        float wv = sin(vArc * u_wave.y + t * u_wave.z + u_seed * 6.2831853);
        float wa = u_wave.x * headWeld * pow(max(seg, 1e-4), max(u_wave.w, 0.35));
        c += side * wv * wa;
        c += B * cos(vArc * u_wave.y * 0.7 + t * u_wave.z * 1.3 + u_seed * 4.0) * wa * 0.35;
    }

    // ── NOISE: turbulence 3D trong world space ──────────────────────────────
    if (u_noise.x > 0.0)
    {
        vec3 q = c * u_noise.y + vec3(0.0, -t * u_noise.z, 0.0) + u_seed * 13.7;
        float na = 2.0 * u_noise.x * headWeld * pow(max(seg, 1e-4), max(u_noise.w, 0.35));
        c += (vec3(VNoise3D(q), VNoise3D(q + vec3(17.1)), VNoise3D(q + vec3(31.7))) - 0.5) * na;
    }

    // ── DRIFT: directional acceleration by age^2 (rising fire / billowing smoke)
    if (dot(u_drift, u_drift) > 0.0001)
    {
        c += u_drift * (seg * seg);
    }

    // ── HELIX: quấn quanh path (Linh Toàn / Thái Cực, world-anchored) ───────
    if (u_helix.x > 0.0)
    {
        float ph = vArc * u_helix.y + t * u_helix.z + u_helix.w;
        c += (side * cos(ph) + B * sin(ph)) * u_helix.x * headWeld * smoothstep(0.0, 0.35, vDist) * (1.0 - 0.35 * seg);
    }

    // ── JITTER: multi-octave quantized zigzag (lightning arc) ────────────────
    if (u_jitter.x > 0.0)
    {
        float tq = floor(t * u_jitter.z) * 13.37 + u_seed * 91.7;
        float x = -vArc * u_jitter.y;
        float amp = 1.0;
        vec2 o = vec2(0.0);
        for (int i = 0; i < 4; i++)
        {
            float fi = float(i);
            vec2 xOff = x + vec2(fi * 7.3, fi * 3.7);
            vec2 i0 = floor(xOff);
            vec2 f0 = fract(xOff);
            vec4 seeds = vec4(i0.x, i0.x + 1.0, i0.y, i0.y + 1.0) +
                         vec4(tq + fi * 3.1, tq + fi * 3.1, tq + fi * 5.9 + 11.0, tq + fi * 5.9 + 11.0);
            vec4 h = fract(sin(seeds) * 43758.5453);
            vec2 z = mix(h.xz, h.yw, f0);
            o += (z - 0.5) * amp;
            x *= 2.1;
            amp *= 0.55;
        }
        c += (side * o.x + B * o.y) * 2.0 * u_jitter.x * headWeld * smoothstep(0.0, max(u_jitter.w, 1e-4), vDist) * (1.0 - 0.3 * seg);
    }

    // ── LANE: bundle nhiều sợi lệch khỏi trục, xoáy theo d (Phong Nhận) ─────
    if (u_lane.x != 0.0)
    {
        float th = vArc * u_lane.z + t * u_lane.w;
        vec3 ax = side * cos(th) + B * sin(th);
        c += ax * u_lane.x * u_lane.y * headWeld * (0.25 + 0.95 * seg);
    }

    // ── WIDTH PROFILING & SHARD SERRATIONS ──────────────────────────────────
    float hw = 1.0;
    if (u_widthProfile.x > 0.001 || u_widthProfile.y > 0.001 || u_widthProfile.z > 0.001 || u_widthProfile.w > 0.001 || u_cap.x > 0.0 || u_cap.y > 0.0)
    {
        float taper = pow(clamp(1.0 - seg, 0.0, 1.0), max(u_widthProfile.x, 0.01)) * mix(1.0, smoothstep(0.0, 0.4, 1.0 - seg), u_cap.y);
        float headLen = max(u_widthProfile.y, 0.0001);
        float ht = clamp(vDist / headLen, 0.0, 1.0);
        float head = (u_widthProfile.y > 0.0) ? mix(sin(ht * 1.5707963), sqrt(max(1.0 - (1.0 - ht) * (1.0 - ht), 0.0)), u_cap.x) : 1.0;
        float bulge = 1.0 + u_widthProfile.w * sin(3.14159265 * seg);
        float grow = 1.0 + u_widthProfile.z * seg;
        float shard = (u_shard.y > 0.0) ? mix(1.0, 0.2 + 0.8 * fract(-vArc * u_shard.x), u_shard.y) : 1.0;
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
        vec3 baseP = pos * u_curlScale;
        float nx = VNoise3D(baseP + vec3(t * 0.31, t * 0.17, t * 0.23));
        float ny = VNoise3D(baseP + vec3(t * 0.17, t * 0.23, t * 0.31));
        vec3 d = sd * (nx - 0.5) + u_stripNormal * (ny - 0.5);
        pos += d * u_waveStrength * env * 2.0;
    }
    else if (u_deformMode >= 0.5)
    {
        vec3 d = MeshDeform_ApplyField(vec2(seg, seg), vec2(seg, seg), t,
                                       sd, u_stripNormal, 3, 3);
        pos += d * env * u_waveStrength;
    }

    vColor = vertexColor;
    vSegUV = vec2(vertexTexCoord.x, seg);
    VS_FinalOutput(pos);
}
