#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;
uniform vec2 u_windDirection;
uniform float u_windStrength;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out float fragHeight;

void main()
{
    vec3 local = vertexPosition;
    vec3 world = vec3(matModel * vec4(local, 1.0));
    float rootMask = vertexTexCoord.y * vertexTexCoord.y;
    float gust = sin(u_time * 1.17 + dot(world.xz, vec2(0.21, 0.17)) + vertexTexCoord.x * 6.2831);
    gust += sin(u_time * 0.43 + dot(world.xz, vec2(-0.08, 0.13))) * 0.45;
    local.xz += u_windDirection * gust * u_windStrength * rootMask;
    world = vec3(matModel * vec4(local, 1.0));

    fragPosition = world;
    fragNormal = normalize(mat3(matModel) * vertexNormal);
    fragColor = vertexColor;
    fragHeight = vertexTexCoord.y;
    gl_Position = mvp * vec4(local, 1.0);
}
