#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform vec3 u_modelPos;
uniform vec2 u_lakeRadii;
uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;
uniform vec2 u_flowVelocity;
uniform int u_waterShape;    // 0 = Radial, 1 = Rect, 2 = Strip, 3 = Custom Depth
uniform vec3 u_waterInteractor;
uniform vec3 u_waterVelocity;
uniform float u_waterRadius;
uniform sampler2D u_waveFieldTex;
uniform int u_waveFieldEnabled;
uniform int u_flowFieldEnabled;
uniform float u_rainRise;    // storm swell (m) applied to deep terrain-water vertices

out vec3 fragPosition;
out vec2 fragLakeCoord;
out vec2 fragWorldXZ;
out float fragShoreFade;
out float fragWaterDepth;
out float fragOceanCrest;
out vec3 fragOceanNormal;

vec3 addOceanGerstner(vec2 xz, vec2 dir, float steepness, float wavelength, float speedMul, float tVal,
                      inout vec3 tangent, inout vec3 binormal, inout float crestAccum)
{
    float k = 6.2831853 / wavelength;
    float c = sqrt(9.81 / k) * speedMul;
    vec2 d = normalize(dir);
    float f = k * (dot(d, xz) - c * tVal);
    float a = steepness / k;
    float s = sin(f);
    float co = cos(f);
    tangent  += vec3(-d.x * d.x * steepness * s, d.x * steepness * co, -d.x * d.y * steepness * s);
    binormal += vec3(-d.x * d.y * steepness * s, d.y * steepness * co, -d.y * d.y * steepness * s);
    crestAccum += steepness * pow(max(0.0, s), 1.4);
    return vec3(d.x * a * co, a * s, d.y * a * co);
}

void main()
{
    vec3 local = vertexPosition;
    vec3 world = (u_waterShape == 3) ? local : (local + u_modelPos);

    // Calculate shore distance factor to smoothly dampen waves near shoreline
    vec2 coord = vertexTexCoord * 2.0 - 1.0;
    float shoreFade = 1.0;

    if (u_waterShape == 0) {
        // Radial lake
        shoreFade = 1.0 - smoothstep(0.75, 0.98, length(coord));
    } else if (u_waterShape == 1) {
        // Rectangular basin
        vec2 edge = smoothstep(vec2(0.80), vec2(0.98), abs(coord));
        shoreFade = (1.0 - edge.x) * (1.0 - edge.y);
    } else if (u_waterShape == 2) {
        // Stream / strip: bank damping along width (Y in local UV)
        shoreFade = 1.0 - smoothstep(0.72, 0.98, abs(coord.y));
    } else if (u_waterShape == 3) {
        // Terrain-clipped water: vertexTexCoord.x is depth in meters (0 at exact shoreline)
        shoreFade = smoothstep(0.0, 0.25, vertexTexCoord.x);
        if (u_lakeRadii.x > 0.001 && u_lakeRadii.y > 0.001) {
            coord = (world.xz - u_modelPos.xz) / u_lakeRadii;
        }
    }

    // Gentle 3D vertical wave undulation damped to 0.0 at the shoreline so edges never detach from terrain
    float oceanCrestOut = 0.0;
    vec3 oceanNormalOut = vec3(0.0, 1.0, 0.0);
    if (u_waterShape == 1 && u_flowFieldEnabled > 0) {
        float t = mod(u_time * u_waveSpeed, 6283.185307);
        float owWaveScale = (u_lakeRadii.x > 500.0) ? (u_lakeRadii.x / 420.0) : 1.0;
        float horizonCalm = 1.0 - smoothstep(0.55, 0.90, max(abs(coord.x), abs(coord.y)));
        float amp = u_waveHeight * 1.45 * shoreFade * horizonCalm;
        vec3 tangent = vec3(1.0, 0.0, 0.0);
        vec3 binormal = vec3(0.0, 0.0, 1.0);
        float crest = 0.0;
        vec3 disp = vec3(0.0);
        // 4 Golden-Ratio Gerstner Swells for crisp 3D wave peaks & analytical normals (Nyquist-safe in OW)
        disp += addOceanGerstner(world.xz, vec2(-0.96,  0.28), 0.24 * amp, 54.0 * owWaveScale, 1.15, t, tangent, binormal, crest);
        disp += addOceanGerstner(world.xz, vec2(-0.91, -0.41), 0.20 * amp, 34.0 * owWaveScale, 1.08, t, tangent, binormal, crest);
        disp += addOceanGerstner(world.xz, vec2(-0.98,  0.16), 0.16 * amp, 22.0 * owWaveScale, 1.02, t, tangent, binormal, crest);
        disp += addOceanGerstner(world.xz, vec2(-0.84,  0.54), 0.13 * amp, 14.0 * owWaveScale, 0.96, t, tangent, binormal, crest);
        local += disp;
        world += disp;
        vec3 nAnalytic = normalize(cross(binormal, tangent));
        if (nAnalytic.y < 0.0) nAnalytic = -nAnalytic;
        oceanNormalOut = nAnalytic;
        oceanCrestOut = clamp(crest * 1.05, 0.0, 1.6);
    } else if (shoreFade > 0.001 && u_waveHeight > 0.0001) {
        // Đồng bộ mod() thời gian với water_surface.fs để tránh trôi pha float32 khi game chạy lâu
        float t = mod(u_time * u_waveSpeed, 6283.185307);
        vec2 effectiveFlow = u_flowVelocity;
        float effFlowLenSq = dot(effectiveFlow, effectiveFlow);
        vec2 normFlowDir = (effFlowLenSq > 1e-6) ? (effectiveFlow * inversesqrt(effFlowLenSq)) : vec2(0.55, 0.83);
        vec2 perpFlow = vec2(-normFlowDir.y, normFlowDir.x);
        float flowSpeed = sqrt(effFlowLenSq);

        vec2 d0 = normalize(normFlowDir * 0.90 + perpFlow * 0.18);
        vec2 d1 = normalize(normFlowDir * 0.90 - perpFlow * 0.18);
        vec2 d2 = normalize(normFlowDir * 0.95 + perpFlow * 0.09);

        // Rushing downstream wave advection (synced with water_surface.fs)
        vec2 warp = vec2(sin(world.z * 0.45 + t * 0.55), cos(world.x * 0.45 - t * 0.45)) * 0.35;
        vec2 pw = world.xz + warp;
        float w0 = sin(dot(pw, d0) * (u_waveScale * 1.1) - t * 2.8);
        float w1 = sin(dot(pw, d1) * (u_waveScale * 1.6) - t * 3.5);
        float w2 = sin(dot(pw, d2) * (u_waveScale * 2.2) - t * 4.4);
        float dispY = (w0 * 0.45 + w1 * 0.35 + w2 * 0.20) * u_waveHeight * shoreFade;
        local.y += dispY;
        world.y += dispY;
    }

    if (u_waterShape == 3 && u_rainRise > 0.0001) {
        // Swell only where the channel is already deep so shorelines never detach from the bank
        float lift = u_rainRise * smoothstep(0.08, 0.50, vertexTexCoord.x);
        local.y += lift;
        world.y += lift;
    }

    fragPosition   = world;
    fragLakeCoord  = coord;
    fragWorldXZ    = world.xz;
    fragShoreFade   = shoreFade;
    fragWaterDepth  = max(0.0, vertexTexCoord.x);
    fragOceanCrest  = oceanCrestOut;
    fragOceanNormal = oceanNormalOut;

    gl_Position = mvp * vec4(local, 1.0);
}
