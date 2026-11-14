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
uniform float u_rainRise;    // storm swell (m) applied to deep terrain-water vertices

out vec3 fragPosition;
out vec2 fragLakeCoord;
out vec2 fragWorldXZ;
out float fragShoreFade;
out float fragWaterDepth;

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
    if (shoreFade > 0.001 && u_waveHeight > 0.0001) {
        // Đồng bộ mod() thời gian với water_surface.fs để tránh trôi pha float32 khi game chạy lâu
        float t = mod(u_time * u_waveSpeed, 6283.185307);
        vec2 effectiveFlow = u_flowVelocity;
        float effFlowLenSq = dot(effectiveFlow, effectiveFlow);
        vec2 normFlowDir = (effFlowLenSq > 1e-6) ? (effectiveFlow * inversesqrt(effFlowLenSq)) : vec2(0.55, 0.83);
        vec2 perpFlow = vec2(-normFlowDir.y, normFlowDir.x);
        float flowSpeed = sqrt(effFlowLenSq);

        vec2 d0 = normalize(normFlowDir * 0.90 + perpFlow * 0.28);
        vec2 d1 = normalize(normFlowDir * 0.90 - perpFlow * 0.28);
        vec2 d2 = normalize(normFlowDir * 0.95 + perpFlow * 0.14);

        // Rushing downstream wave advection (synced with water_surface.fs)
        vec2 warp = vec2(sin(world.z * 0.45 + t * 0.55), cos(world.x * 0.45 - t * 0.45)) * 0.35;
        vec2 pw = world.xz + warp;
        float w0 = sin(dot(pw, d0) * (u_waveScale * 1.6) - t * 3.6);
        float w1 = sin(dot(pw, d1) * (u_waveScale * 2.2) - t * 4.4);
        float w2 = sin(dot(pw, d2) * (u_waveScale * 3.4) - t * 6.2);
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
    fragShoreFade  = shoreFade;
    fragWaterDepth = max(0.0, vertexTexCoord.x);

    gl_Position = mvp * vec4(local, 1.0);
}
