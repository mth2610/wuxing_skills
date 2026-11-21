#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform float u_time;
uniform float u_waveHeight;
uniform vec2 u_flowDirection;
uniform float u_flowSpeed;

out vec3 fragWorldPos;
out float fragLavaDepth;
out float fragShoreFade;

void main()
{
    vec3 pos = vertexPosition;
    float depth = max(0.0, vertexTexCoord.x);
    float shoreFade = smoothstep(0.0, 0.28, depth);

    if (shoreFade > 0.001 && u_waveHeight > 0.0001) {
        float t = mod(u_time * max(u_flowSpeed, 0.1), 6283.185307);
        vec2 dir = (dot(u_flowDirection, u_flowDirection) > 1e-5) ? normalize(u_flowDirection) : vec2(0.0, 1.0);
        float p0 = dot(pos.xz, dir) * 0.65 - t * 0.90;
        float p1 = dot(pos.xz, vec2(-dir.y, dir.x)) * 0.85 + t * 0.55;
        float heave = (sin(p0) * 0.60 + cos(p1) * 0.40) * u_waveHeight * shoreFade;
        pos.y += heave;
    }

    fragWorldPos = pos;
    fragLavaDepth = depth;
    fragShoreFade = shoreFade;

    gl_Position = mvp * vec4(pos, 1.0);
}
