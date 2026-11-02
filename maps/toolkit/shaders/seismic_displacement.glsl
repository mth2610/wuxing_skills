// ============================================================================
// seismic_displacement.glsl - Physical Ground Wave Displacement
// Evaluates real-time 3D wave undulation for terrain and vegetation vertex shaders.
// ============================================================================

#ifndef SEISMIC_DISPLACEMENT_GLSL
#define SEISMIC_DISPLACEMENT_GLSL

uniform vec4 u_seismicOriginDir;  // xy: source.xz, zw: fwdNorm.xz
uniform vec4 u_seismicParams;     // x: frontDist, y: totalDist, z: waveAmplitude, w: waveWidth
uniform vec4 u_seismicProfile;    // x: wavelength, y: packetLength, z: rippleCount, w: active (1.0 or 0.0)

float EvaluateSeismicDisplacement(vec2 worldXZ, out vec2 outSlope)
{
    outSlope = vec2(0.0);
    float amp = u_seismicParams.z;
    if (amp <= 0.0001 || u_seismicProfile.w <= 0.5) return 0.0;

    vec2 src = u_seismicOriginDir.xy;
    vec2 fwd = u_seismicOriginDir.zw;
    vec2 right = vec2(-fwd.y, fwd.x);

    vec2 dPos = worldXZ - src;
    float s = dot(dPos, fwd);
    float w = dot(dPos, right);

    float frontDist = u_seismicParams.x;
    float totalDist = u_seismicParams.y;
    float waveWidth = u_seismicParams.w;
    float wl = max(u_seismicProfile.x, 0.6);
    float pLen = max(u_seismicProfile.y, 1.0);
    float rippleCount = u_seismicProfile.z;

    float sRear = frontDist - pLen;
    if (s > frontDist || s < sRear) return 0.0;

    float halfW = max(waveWidth * 0.5, 0.2);
    if (abs(w) >= halfW) return 0.0;

    float p = (s - sRear) / pLen;
    if (p <= 0.0 || p >= 1.0) return 0.0;

    // Smooth bell envelope (strictly 0 at rear and 0 at front with zero derivative)
    float sinP = sin(3.14159265 * p);
    float envelope = sinP * sqrt(max(sinP, 0.0));

    // Lateral profile: smooth cosine taper
    float q = w / halfW;
    float latProfile = 0.5 * (1.0 + cos(3.14159265 * q));

    // Longitudinal boundary fades
    float sFade = clamp(s / 1.0, 0.0, 1.0);
    float eFade = clamp((totalDist - s) / 0.6, 0.0, 1.0);

    // Wave shape within packet
    float u = (frontDist - s) / wl;
    float d0 = (u - 0.38) / 0.28;
    float crest0 = exp(-0.5 * d0 * d0);

    float crest1 = 0.0;
    if (rippleCount >= 1.5) {
        float d1 = (u - 1.45) / 0.32;
        crest1 = 0.58 * exp(-0.5 * d1 * d1);
    }
    float crest2 = 0.0;
    if (rippleCount >= 2.5) {
        float d2 = (u - 2.50) / 0.36;
        crest2 = 0.34 * exp(-0.5 * d2 * d2);
    }
    float dt0 = (u - 0.92) / 0.26;
    float trough0 = -0.15 * exp(-0.5 * dt0 * dt0);

    float waveShape = crest0 + crest1 + crest2 + trough0;

    float dY = amp * waveShape * envelope * latProfile * sFade * eFade;
    dY = max(dY, -0.12 * amp);

    // Finite difference slope along forward (s) and lateral (w)
    float ds = 0.18;
    float sNext = s + ds;
    if (sNext <= frontDist && sNext >= sRear) {
        float pN = (sNext - sRear) / pLen;
        float sinPN = sin(3.14159265 * pN);
        float envN = sinPN * sqrt(max(sinPN, 0.0));
        float uN = (frontDist - sNext) / wl;
        float c0N = exp(-0.5 * ((uN - 0.38) / 0.28) * ((uN - 0.38) / 0.28));
        float c1N = (rippleCount >= 1.5) ? (0.58 * exp(-0.5 * ((uN - 1.45) / 0.32) * ((uN - 1.45) / 0.32))) : 0.0;
        float c2N = (rippleCount >= 2.5) ? (0.34 * exp(-0.5 * ((uN - 2.50) / 0.36) * ((uN - 2.50) / 0.36))) : 0.0;
        float t0N = -0.15 * exp(-0.5 * ((uN - 0.92) / 0.26) * ((uN - 0.92) / 0.26));
        float shapeN = c0N + c1N + c2N + t0N;
        float sFadeN = clamp(sNext / 1.0, 0.0, 1.0);
        float eFadeN = clamp((totalDist - sNext) / 0.6, 0.0, 1.0);
        float dYN = max(amp * shapeN * envN * latProfile * sFadeN * eFadeN, -0.12 * amp);
        outSlope.x = (dYN - dY) / ds;
    }

    float dw = 0.18;
    float wNext = abs(w) + dw;
    if (wNext < halfW) {
        float qN = wNext / halfW;
        float latN = 0.5 * (1.0 + cos(3.14159265 * qN));
        float dYW = max(amp * waveShape * envelope * latN * sFade * eFade, -0.12 * amp);
        float signW = (w >= 0.0) ? 1.0 : -1.0;
        outSlope.y = signW * (dYW - dY) / dw;
    }

    return dY;
}

vec3 PerturbNormalWithSeismicWave(vec3 baseNormal, vec2 slope)
{
    if (abs(slope.x) < 0.001 && abs(slope.y) < 0.001) return baseNormal;
    vec2 fwd = u_seismicOriginDir.zw;
    vec2 right = vec2(-fwd.y, fwd.x);

    // Gradient of heightfield: g = fwd * slope.x + right * slope.y
    vec2 grad = fwd * slope.x + right * slope.y;

    // Perturb surface normal: surface normal tilted by wave slope
    vec3 perturbed = vec3(baseNormal.x - grad.x, baseNormal.y, baseNormal.z - grad.y);
    float lenSq = dot(perturbed, perturbed);
    return (lenSq > 1e-6) ? (perturbed * inversesqrt(lenSq)) : baseNormal;
}

#endif // SEISMIC_DISPLACEMENT_GLSL
