#version 430 core
struct GranularGrain {
    vec4 position_radius;
    vec4 velocity_life;
    vec4 meta_ao;
    vec4 aux_pad;
};

layout(std430, binding = 0) readonly buffer GranularState { GranularGrain grains[]; };

in vec3 vertexPosition;

uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_aspectRatio;
uniform float u_time;

out vec2 v_corner;
out float v_life;
out float v_seed;
out float v_contactAO;
out float v_materialId;
out float v_speed;
out float v_flags;
out float v_tiltShade;

void main()
{
    GranularGrain g = grains[gl_InstanceID];
    v_life = g.velocity_life.w;
    v_seed = g.meta_ao.x;
    v_materialId = g.meta_ao.y;
    v_contactAO = g.meta_ao.z;
    v_flags = g.meta_ao.w;
    v_corner = vertexPosition.xy;
    v_speed = length(g.velocity_life.xyz);
    v_tiltShade = 1.0;

    if (v_life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    int matId = int(v_materialId + 0.5);
    bool isLamina = (matId == 5 || matId == 6 || matId == 8);
    bool isTwig   = (matId == 7);
    bool isShard  = (matId == 9);
    bool isFineSand = (matId == 0 || matId == 3 || matId == 4 || matId == 10);

    vec3 centerView = (u_view * vec4(g.position_radius.xyz, 1.0)).xyz;

    if (isLamina) {
        // 3D tumbling & fluttering planar lamina (leaves & petals)
        float r = g.position_radius.w * 2.15 * mix(0.80, 1.30, fract(v_seed * 17.31));
        bool resting = (v_flags > 0.5 && v_flags < 1.5);
        float yaw = v_seed * 6.2831853 + (resting ? 0.0 : u_time * (2.5 + 3.5 * fract(v_seed * 13.7)));
        float pitch = resting ? 0.12 * sin(v_seed * 41.0) : (u_time * (3.2 + 4.8 * fract(v_seed * 23.1)) + v_seed * 6.283);
        float roll  = resting ? 0.10 * cos(v_seed * 29.0) : sin(u_time * 4.5 + v_seed * 19.0) * 0.85;

        float cy = cos(yaw), sy = sin(yaw);
        float cp = cos(pitch), sp = sin(pitch);
        float cr = cos(roll), sr = sin(roll);

        // Local 3D leaf frame in world space: flat on XZ when resting
        vec3 lFwd = vec3(cy * cp, sp, sy * cp);
        vec3 lUp  = vec3(-sy * sr - cy * sp * cr, cp * cr, cy * sr - sy * sp * cr);
        vec3 lRight = normalize(cross(lUp, lFwd));

        mat3 vRot = mat3(u_view);
        vec3 viewFwd = vRot * lFwd;
        vec3 viewRight = vRot * lRight;
        vec3 viewUp = vRot * lUp;
        v_tiltShade = 0.65 + 0.35 * abs(viewUp.z);

        float aspectW = (matId == 8) ? 0.78 : 0.60; // Petals broader than willow/oak leaves
        vec3 worldOff = lFwd * (vertexPosition.x * r) + lRight * (vertexPosition.y * r * aspectW);
        vec3 pView = (u_view * vec4(g.position_radius.xyz + worldOff, 1.0)).xyz;
        // Nudge slightly toward camera when resting so ground-hugging leaves don't z-fight
        pView.z += resting ? 0.015 : 0.0;
        gl_Position = u_projection * vec4(pView, 1.0);
        return;
    }

    float baseScale = isFineSand ? 1.45 : (isTwig ? 1.85 : (isShard ? 1.65 : 1.55));
    float r = g.position_radius.w * baseScale * mix(0.80, 1.25, fract(v_seed * 17.31));

    // Project 3D velocity onto camera view plane (GPU_BACKEND_LANDMINES)
    vec3 velView = mat3(u_view) * g.velocity_life.xyz;
    float speedXY = length(velView.xy);
    float stretch = isTwig ? 2.40 : clamp(1.0 + speedXY * 0.24, 1.0, 2.2);
    float crossScale = isTwig ? 0.42 : (inversesqrt(stretch) * mix(0.76, 1.0, u_aspectRatio));
    if (isShard) { stretch = clamp(1.35 + speedXY * 0.18, 1.35, 2.4); crossScale *= 0.72; }

    float rotAngle = v_seed * 6.2831853 + (isShard && v_speed > 0.2 ? u_time * 4.0 * fract(v_seed * 11.1) : 0.0);
    vec2 defaultAxis = vec2(cos(rotAngle), sin(rotAngle));
    vec2 axis = speedXY > 0.2 ? velView.xy / speedXY : defaultAxis;
    vec2 perp = vec2(-axis.y, axis.x);

    vec2 offsetView = (axis * (vertexPosition.x * stretch) + perp * (vertexPosition.y * crossScale)) * r;
    gl_Position = u_projection * vec4(centerView + vec3(offsetView, 0.0), 1.0);
}
