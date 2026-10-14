#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform vec3 u_modelPos;
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

out vec3 fragPosition;
out vec2 fragLakeCoord;
out vec2 fragWorldXZ;
out float fragShoreFade;
out float fragWaterDepth;

void main()
{
    vec3 local = vertexPosition;
    vec3 world = local + u_modelPos;

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
        shoreFade = smoothstep(0.0, 0.25, vertexTexCoord.x);
    }

    fragPosition   = world;
    fragLakeCoord  = coord;
    fragWorldXZ    = world.xz;
    fragShoreFade  = shoreFade;
    fragWaterDepth = max(0.0, vertexTexCoord.x);

    gl_Position = mvp * vec4(local, 1.0);
}