uniform vec3 u_windBaseVelocity;
uniform float u_windGustAmplitude;
uniform float u_windNoiseScale;
uniform float u_windNoiseSpeed;
uniform int u_windFieldDetail;
// x = response lag, y = transverse flutter gain,
// z = flutter frequency, w = local/compliance response scale.
uniform vec4 u_natureWindResponse;
uniform sampler2D u_windCacheMap;
uniform vec4 u_windCacheRegion;
uniform float u_windCacheScale;
uniform vec2 u_windCacheTimeRange;
uniform int u_windCacheEnabled;

vec3 NatureWindHash3(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

float NatureWindNoiseScalar3D(vec3 p)
{
    vec3 lattice = floor(p);
    vec3 f = p - lattice;
    // Quintic Hermite interpolation (C2 continuous) for smoother wind acceleration
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 f1 = f - 1.0;

    float n000 = dot(NatureWindHash3(lattice), f);
    float n100 = dot(NatureWindHash3(lattice + vec3(1.0, 0.0, 0.0)), vec3(f1.x, f.y,  f.z));
    float n010 = dot(NatureWindHash3(lattice + vec3(0.0, 1.0, 0.0)), vec3(f.x,  f1.y, f.z));
    float n110 = dot(NatureWindHash3(lattice + vec3(1.0, 1.0, 0.0)), vec3(f1.x, f1.y, f.z));
    float n001 = dot(NatureWindHash3(lattice + vec3(0.0, 0.0, 1.0)), vec3(f.x,  f.y,  f1.z));
    float n101 = dot(NatureWindHash3(lattice + vec3(1.0, 0.0, 1.0)), vec3(f1.x, f.y,  f1.z));
    float n011 = dot(NatureWindHash3(lattice + vec3(0.0, 1.0, 1.0)), vec3(f.x,  f1.y, f1.z));
    float n111 = dot(NatureWindHash3(lattice + vec3(1.0, 1.0, 1.0)), f1);

    // Vectorized trilinear reduction
    vec4 nx = mix(vec4(n000, n010, n001, n011), vec4(n100, n110, n101, n111), u.x);
    vec2 nxy = mix(nx.xz, nx.yw, u.y);
    return mix(nxy.x, nxy.y, u.z);
}

vec2 NatureGlobalWindXZ(vec3 worldPosition, float time)
{
    vec3 baseVelocity = u_windBaseVelocity;
    float baseSpeedSq = dot(baseVelocity, baseVelocity);
    if (baseSpeedSq < 1e-8)
        return vec2(0.0);
    if (u_windGustAmplitude <= 0.0001)
        return baseVelocity.xz;

    // Fast height & bounding-box check BEFORE querying textureSize(u_windCacheMap, 0)
    if (u_windCacheEnabled != 0 && u_windCacheRegion.z > 0.0001 && abs(worldPosition.y - u_windCacheRegion.w) < 2.0) {
        // Apply Taylor's frozen-turbulence spatial shift when response lag is large
        float extraLag = max(0.0, u_natureWindResponse.x - 0.08);
        vec2 sampleXZ = worldPosition.xz - baseVelocity.xz * (extraLag * 0.45);
        vec2 uvOffset = (sampleXZ - u_windCacheRegion.xy) / u_windCacheRegion.z;
        float maxAxis = max(abs(uvOffset.x), abs(uvOffset.y));

        if (maxAxis < 0.5) {
            float texWidth = max(float(textureSize(u_windCacheMap, 0).x), 1.0);
            float halfTexel = 0.5 / texWidth;
            if (maxAxis <= (0.5 - halfTexel)) {
                vec4 cached = textureLod(u_windCacheMap, uvOffset + 0.5, 0.0);
                float fraction = clamp((time - u_windCacheTimeRange.x) / max(u_windCacheTimeRange.y, 0.0001), 0.0, 1.0);
                vec2 velocity = mix(cached.rg, cached.ba, fraction);
                return (velocity * 2.0 - 1.0) * u_windCacheScale;
            }
        }
    }

    float baseSpeed = sqrt(baseSpeedSq);
    float s = u_windNoiseScale;
    float speed = u_windNoiseSpeed;
    // Giữ ts trong miền float32 chính xác cao khi game chạy nhiều giờ liên tục
    float ts = mod(time * speed, 16384.0);
    vec3 scaledPos = worldPosition * s;

    float nx = NatureWindNoiseScalar3D(vec3(
        scaledPos.x - ts,
        scaledPos.y + 17.3,
        scaledPos.z - ts * 0.7));

    float halfGust = u_windGustAmplitude * 0.5;
    float halfAmp  = halfGust * max(baseSpeed, 2.0);

    if (u_windFieldDetail == 0) {
        vec2 baseXZ = baseVelocity.xz;
        float horizSpeedSq = dot(baseXZ, baseXZ);
        vec2 direction = (horizSpeedSq > 1e-8) ? (baseXZ * inversesqrt(horizSpeedSq)) : vec2(0.0);
        return baseXZ * (1.0 + halfGust * nx) + direction * (nx * halfAmp);
    }

    float nz = NatureWindNoiseScalar3D(vec3(
        scaledPos.x - ts * 0.6,
        scaledPos.y + 53.9,
        scaledPos.z + ts * 0.5));

    return vec2(
        baseVelocity.x * (1.0 + halfGust * nx) + nx * halfAmp,
        baseVelocity.z * (1.0 + halfGust * nz) + nz * halfAmp
    );
}

vec2 NatureVegetationWindBend(vec3 worldPosition, vec2 phaseUV,
                              float time, float compliance, float maxBend)
{
    vec2 velocity = NatureGlobalWindXZ(
        worldPosition, max(0.0, time - u_natureWindResponse.x));

    float speedSq = dot(velocity, velocity);
    if (speedSq <= 1e-8 || compliance <= 1e-6 || maxBend <= 1e-5)
        return vec2(0.0);

    float invSpeed = inversesqrt(speedSq);
    float speed = speedSq * invSpeed;
    float bendLength = speed * compliance;
    vec2 windDirection = velocity * invSpeed;
    vec2 transverse = vec2(-windDirection.y, windDirection.x);

    // Dual-harmonic flutter using both phaseUV channels without extra trig calls
    // Dùng mod(..., 6283.185307) (1000 * 2pi) để bảo toàn độ chính xác sin/cos khi u_time lớn
    float phase = mod(time * u_natureWindResponse.z, 6283.185307)
                + dot(worldPosition.xz, vec2(0.55, -0.45))
                + dot(phaseUV, vec2(6.2831853, 3.1415926));
    float s1 = sin(phase);
    float c1 = cos(phase);
    float s2 = 2.0 * s1 * c1; // sin(2 * phase)

    // Asymmetric spring recoil along wind direction + rich transverse flutter
    float axialPulse = (s1 * 0.12 + s2 * 0.05) * u_natureWindResponse.y;
    float lateralFlutter = (s1 * 0.78 + s2 * 0.22) * u_natureWindResponse.y;

    vec2 bend = windDirection * (bendLength * (1.0 + axialPulse))
              + transverse * (bendLength * lateralFlutter);

    // [TỐI ƯU ALU]: Chỉ tính inversesqrt(dispSq) khi độ uốn thực sự vượt ngưỡng kneeStart (70% maxBend),
    // giúp bỏ qua hoàn toàn căn bậc hai cho đa số đỉnh cỏ trong điều kiện gió bình thường.
    float dispSq = dot(bend, bend);
    float kneeStart = maxBend * 0.70;
    if (dispSq > kneeStart * kneeStart) {
        float invDisp = inversesqrt(dispSq);
        float dispLen = dispSq * invDisp;
        float span = max(maxBend - kneeStart, 1e-5);
        float excess = (dispLen - kneeStart) / span;
        float softLen = kneeStart + span * (excess / (1.0 + excess));
        bend *= (softLen * invDisp);
    }
    return bend;
}
