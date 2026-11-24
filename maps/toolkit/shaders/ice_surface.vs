#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform float u_time;
uniform vec2 u_driftDirection;
uniform float u_driftSpeed;

out vec3 fragWorldPos;
out float fragIceDepth;
out float fragShoreFade;

void main()
{
    vec3 pos = vertexPosition;
    float depth = max(0.0, vertexTexCoord.x);
    float shoreFade = smoothstep(0.0, 0.25, depth);

    // Gentle glacial floe bobbing in deeper water leads
    if (shoreFade > 0.001) {
        float t = mod(u_time * max(u_driftSpeed, 0.05), 6283.185307);
        vec2 dir = (dot(u_driftDirection, u_driftDirection) > 1e-5) ? normalize(u_driftDirection) : vec2(0.7, 0.7);
        float bob = sin(dot(pos.xz, dir) * 0.45 - t * 2.2) * 0.006 * shoreFade;
        pos.y += bob;
    }

    fragWorldPos = pos + vec3(0.0, 0.014, 0.0);
    fragIceDepth = depth;
    fragShoreFade = shoreFade;

    gl_Position = mvp * vec4(pos, 1.0);
}
