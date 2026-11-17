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

out vec2 v_corner;
out float v_life;
out float v_seed;
out float v_contactAO;
out float v_materialId;
out float v_speed;

void main()
{
    GranularGrain g = grains[gl_InstanceID];
    v_life = g.velocity_life.w;
    v_seed = g.meta_ao.x;
    v_materialId = g.meta_ao.y;
    v_contactAO = g.meta_ao.z;
    v_corner = vertexPosition.xy;
    v_speed = length(g.velocity_life.xyz);

    if (v_life <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec3 centerView = (u_view * vec4(g.position_radius.xyz, 1.0)).xyz;

    // Fine sand (0, 3, 4) overlaps into a cohesive granular continuum;
    // gravel/soil (1, 2) stays as discrete angular clods & pebbles.
    bool isFineSand = (v_materialId < 0.5) || (v_materialId > 2.5);
    float baseScale = isFineSand ? 1.45 : 1.55;
    float r = g.position_radius.w * baseScale * mix(0.80, 1.25, fract(v_seed * 17.31));

    // Project 3D velocity onto camera view plane (GPU_BACKEND_LANDMINES)
    vec3 velView = mat3(u_view) * g.velocity_life.xyz;
    float speedXY = length(velView.xy);
    float stretch = clamp(1.0 + speedXY * 0.24, 1.0, 2.2);
    float crossScale = inversesqrt(stretch) * mix(0.76, 1.0, u_aspectRatio);

    float rotAngle = v_seed * 6.2831853;
    vec2 defaultAxis = vec2(cos(rotAngle), sin(rotAngle));
    vec2 axis = speedXY > 0.2 ? velView.xy / speedXY : defaultAxis;
    vec2 perp = vec2(-axis.y, axis.x);

    vec2 offsetView = (axis * (vertexPosition.x * stretch) + perp * (vertexPosition.y * crossScale)) * r;
    gl_Position = u_projection * vec4(centerView + vec3(offsetView, 0.0), 1.0);
}
