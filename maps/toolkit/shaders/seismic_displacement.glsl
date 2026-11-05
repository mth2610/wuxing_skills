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
    if (s >= frontDist || s <= sRear) return 0.0;

    float halfW = max(waveWidth * 0.5, 0.2);
    if (abs(w) >= halfW) return 0.0;

    float invPLen = 1.0 / pLen;
    float invHalfW = 1.0 / halfW;
    float invWl = 1.0 / wl;

    float p = (s - sRear) * invPLen;

    // Smooth bell envelope: sin(pi * p)^1.5 and its analytical derivative
    float piP = 3.14159265 * p;
    float sinP = max(sin(piP), 0.0);
    float cosP = cos(piP);
    float sqrtSinP = sqrt(sinP);
    float envelope = sinP * sqrtSinP;
    float dEnvDs = (1.5 * 3.14159265 * invPLen) * sqrtSinP * cosP;

    // Lateral profile: 0.5 * (1 + cos(pi * q)) and its analytical derivative
    float piQ = 3.14159265 * (w * invHalfW);
    float latProfile = 0.5 * (1.0 + cos(piQ));
    float dLatDw = (-0.5 * 3.14159265 * invHalfW) * sin(piQ);

    // Longitudinal boundary fades
    float sFade = clamp(s, 0.0, 1.0);
    float eFade = clamp((totalDist - s) * 1.6666667, 0.0, 1.0);
    float fadeProd = sFade * eFade;

    // Wave shape within packet & exact analytical derivative d(waveShape)/ds
    // For G(u) = A * exp(-0.5 * d^2) with d = (u - u0) * invSigma and u = (frontDist - s) / wl:
    // dG/ds = (-1/wl) * dG/du = (1/wl) * G(u) * d * invSigma
    float u = (frontDist - s) * invWl;

    float d0 = (u - 0.38) * 3.5714286; // 1.0 / 0.28
    float crest0 = exp(-0.5 * d0 * d0);
    float dShapeDu = -crest0 * d0 * 3.5714286;

    float crest1 = 0.0;
    if (rippleCount >= 1.5) {
        float d1 = (u - 1.45) * 3.125; // 1.0 / 0.32
        crest1 = 0.58 * exp(-0.5 * d1 * d1);
        dShapeDu -= crest1 * d1 * 3.125;
    }
    float crest2 = 0.0;
    if (rippleCount >= 2.5) {
        float d2 = (u - 2.50) * 2.7777778; // 1.0 / 0.36
        crest2 = 0.34 * exp(-0.5 * d2 * d2);
        dShapeDu -= crest2 * d2 * 2.7777778;
    }
    float dt0 = (u - 0.92) * 3.8461538; // 1.0 / 0.26
    float trough0 = -0.15 * exp(-0.5 * dt0 * dt0);
    dShapeDu -= trough0 * dt0 * 3.8461538;

    float waveShape = crest0 + crest1 + crest2 + trough0;
    float dShapeDs = -dShapeDu * invWl;

    float rawDY = amp * waveShape * envelope * latProfile * fadeProd;
    float minFloor = -0.12 * amp;
    if (rawDY <= minFloor) {
        return minFloor;
    }

    outSlope.x = amp * latProfile * fadeProd * (dShapeDs * envelope + waveShape * dEnvDs);
    outSlope.y = amp * waveShape * envelope * fadeProd * dLatDw;
    return rawDY;
}

vec3 PerturbNormalWithSeismicWave(vec3 baseNormal, vec2 slope)
{
    if (dot(slope, slope) < 1e-6) return baseNormal;
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