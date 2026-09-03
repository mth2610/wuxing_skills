#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;
uniform float u_waveHeight;
uniform float u_waveScale;
uniform float u_waveSpeed;

out vec3 fragPosition;
out vec2 fragLakeCoord;
out vec2 fragWorldXZ;

float waves(vec2 p)
{
    float t = u_time * u_waveSpeed;
    return sin(dot(p, vec2(0.82, 0.57)) * u_waveScale + t) * 0.50
         + sin(dot(p, vec2(-0.31, 0.95)) * u_waveScale * 1.73 - t * 1.31) * 0.29
         + sin(dot(p, vec2(0.96, -0.18)) * u_waveScale * 2.61 + t * 0.73) * 0.16;
}

void main()
{
    vec3 local = vertexPosition;
    vec3 world = vec3(matModel * vec4(local, 1.0));
    float shoreFade = 1.0 - smoothstep(0.76, 1.0, length(vertexTexCoord * 2.0 - 1.0));
    local.y += waves(world.xz) * u_waveHeight * shoreFade;
    world = vec3(matModel * vec4(local, 1.0));
    fragPosition = world;
    fragLakeCoord = vertexTexCoord * 2.0 - 1.0;
    fragWorldXZ = world.xz;
    gl_Position = mvp * vec4(local, 1.0);
}
