uniform vec3 u_windBaseVelocity;
uniform float u_windGustAmplitude;
uniform float u_windNoiseScale;
uniform float u_windNoiseSpeed;
uniform int u_windFieldDetail;
// x = response lag, y = transverse flutter gain,
// z = flutter frequency, w = local/compliance response scale.
uniform vec4 u_natureWindResponse;

vec3 NatureWindHash3(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
}

float NatureWindNoiseScalar3D(vec3 p)
{
    vec3 lattice = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = dot(NatureWindHash3(lattice), f);
    float n100 = dot(NatureWindHash3(lattice + vec3(1.0, 0.0, 0.0)),
                     f - vec3(1.0, 0.0, 0.0));
    float n010 = dot(NatureWindHash3(lattice + vec3(0.0, 1.0, 0.0)),
                     f - vec3(0.0, 1.0, 0.0));
    float n110 = dot(NatureWindHash3(lattice + vec3(1.0, 1.0, 0.0)),
                     f - vec3(1.0, 1.0, 0.0));
    float n001 = dot(NatureWindHash3(lattice + vec3(0.0, 0.0, 1.0)),
                     f - vec3(0.0, 0.0, 1.0));
    float n101 = dot(NatureWindHash3(lattice + vec3(1.0, 0.0, 1.0)),
                     f - vec3(1.0, 0.0, 1.0));
    float n011 = dot(NatureWindHash3(lattice + vec3(0.0, 1.0, 1.0)),
                     f - vec3(0.0, 1.0, 1.0));
    float n111 = dot(NatureWindHash3(lattice + vec3(1.0)),
                     f - vec3(1.0));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);
    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

// Mirrors Wind_GetMacroAt's horizontal field. Grass waves therefore occupy
// the same world-space gusts that move particles, smoke, and other receivers.
vec2 NatureGlobalWindXZ(vec3 worldPosition, float time)
{
    vec3 baseVelocity = u_windBaseVelocity;
    float baseSpeed = length(baseVelocity);
    if (baseSpeed < 0.0001)
        return vec2(0.0);
    if (u_windGustAmplitude <= 0.0001)
        return baseVelocity.xz;

    float s = u_windNoiseScale;
    float speed = u_windNoiseSpeed;
    float nx = NatureWindNoiseScalar3D(vec3(
        worldPosition.x * s - time * speed,
        worldPosition.y * s + 17.3,
        worldPosition.z * s - time * speed * 0.7));
    float amplitude = u_windGustAmplitude * max(baseSpeed, 2.0);
    if (u_windFieldDetail == 0) {
        vec2 baseXZ = baseVelocity.xz;
        float horizontalSpeed = length(baseXZ);
        vec2 direction = horizontalSpeed > 0.0001
            ? baseXZ / horizontalSpeed : vec2(0.0);
        return baseXZ * (1.0 + u_windGustAmplitude * nx * 0.5) +
               direction * nx * amplitude * 0.5;
    }
    float nz = NatureWindNoiseScalar3D(vec3(
        worldPosition.x * s - time * speed * 0.6,
        worldPosition.y * s + 53.9,
        worldPosition.z * s + time * speed * 0.5));
    return vec2(
        baseVelocity.x * (1.0 + u_windGustAmplitude * nx * 0.5) +
            nx * amplitude * 0.5,
        baseVelocity.z * (1.0 + u_windGustAmplitude * nz * 0.5) +
            nz * amplitude * 0.5);
}

vec2 NatureVegetationWindBend(vec3 worldPosition, vec2 phaseUV,
                              float time, float compliance, float maxBend)
{
    vec2 velocity = NatureGlobalWindXZ(
        worldPosition, max(0.0, time - u_natureWindResponse.x));
    vec2 bend = velocity * compliance;
    float bendLength = length(bend);
    float speed = length(velocity);
    if (speed > 0.0001 && bendLength > 0.0001) {
        vec2 windDirection = velocity / speed;
        vec2 transverse = vec2(-windDirection.y, windDirection.x);
        float flutter = sin(time * u_natureWindResponse.z +
                            dot(worldPosition.xz, vec2(0.55, -0.45)) +
                            phaseUV.x * 6.2831);
        bend += transverse * flutter * bendLength * u_natureWindResponse.y;
    }
    float displacedLength = length(bend);
    if (displacedLength > maxBend && displacedLength > 0.0001)
        bend *= maxBend / displacedLength;
    return bend;
}
