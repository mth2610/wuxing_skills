#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform vec3 u_modelPos;
uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;
uniform vec2 u_flowVelocity; // Directional flow vector (x, z)
uniform int u_waterShape;    // 0 = Radial, 1 = Rect, 2 = Strip

out vec3 fragPosition;
out vec2 fragLakeCoord;
out vec2 fragWorldXZ;
out vec4 fragScreenPos;
out float fragShoreFade;

float waves(vec2 p)
{
    float t = u_time * u_waveSpeed;
    // If directional flow is active, shift wave phases along the flow vector
    vec2 flowOffset = u_flowVelocity * (u_time * 0.5);
    vec2 pFlow = p - flowOffset;

    return sin(dot(pFlow, vec2(0.82, 0.57)) * u_waveScale + t) * 0.50
         + sin(dot(pFlow, vec2(-0.31, 0.95)) * u_waveScale * 1.73 - t * 1.31) * 0.29
         + sin(dot(pFlow, vec2(0.96, -0.18)) * u_waveScale * 2.61 + t * 0.73) * 0.16;
}

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
        float edgeX = smoothstep(0.80, 0.98, abs(coord.x));
        float edgeY = smoothstep(0.80, 0.98, abs(coord.y));
        shoreFade = (1.0 - edgeX) * (1.0 - edgeY);
    } else if (u_waterShape == 2) {
        // Stream / strip: bank damping along width (Y in local UV)
        shoreFade = 1.0 - smoothstep(0.72, 0.98, abs(coord.y));
    }

    // In shallow water (<= 1.3m), wave amplitude naturally dampens at the shore
    float wave = waves(world.xz) * u_waveHeight * shoreFade;
    local.y += wave;
    world.y += wave;

    fragPosition = world;
    fragLakeCoord = coord;
    fragWorldXZ = world.xz;
    fragShoreFade = shoreFade;

    vec4 clipPos = mvp * vec4(local, 1.0);
    fragScreenPos = clipPos;
    gl_Position = clipPos;
}
