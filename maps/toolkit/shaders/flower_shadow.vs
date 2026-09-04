#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform vec3 u_lightTravel;

out vec4 fragColor;

void main()
{
    float along = vertexTexCoord.x;
    float across = vertexTexCoord.y * 2.0 - 1.0;
    float plantHeight = vertexNormal.x;
    float shadowWidth = vertexNormal.y;
    vec2 travel = u_lightTravel.xz / max(-u_lightTravel.y, 0.22);
    float travelLength = length(travel);
    vec2 direction = travelLength > 0.001 ? travel / travelLength : vec2(1.0, 0.0);
    vec2 perpendicular = vec2(-direction.y, direction.x);
    vec3 position = vertexPosition;
    position.xz += travel * plantHeight * along;
    position.xz += perpendicular * shadowWidth * across * mix(1.0, 0.34, along);
    fragColor = vertexColor;
    gl_Position = mvp * vec4(position, 1.0);
}
