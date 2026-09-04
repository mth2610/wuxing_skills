#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
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
out vec2 fragTexCoord;

void main()
{
    vec3 local = vertexPosition;
    vec3 world = vec3(matModel * vec4(local, 1.0));
    float rootMask = vertexTexCoord.y * vertexTexCoord.y;
    // Broad gusts stay coherent across the field; per-plant phase only
    // contributes a restrained high-frequency flutter.
    float gust = sin(u_time * 0.74 + dot(world.xz, vec2(0.145, 0.096)));
    gust += sin(u_time * 0.31 + dot(world.xz, vec2(-0.052, 0.081))) * 0.48;
    gust += sin(u_time * 2.35 + dot(world.xz, vec2(0.61, -0.38))
                + vertexTexCoord.x * 6.2831) * 0.16;
    local.xz += u_windDirection * gust * u_windStrength * rootMask;
    world = vec3(matModel * vec4(local, 1.0));

    fragPosition = world;
    fragNormal = normalize(mat3(matModel) * vertexNormal);
    fragColor = vertexColor;
    fragHeight = vertexTexCoord.y;
    fragTexCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(local, 1.0);
}
