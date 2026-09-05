#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 u_lightTravel;
uniform float u_projectionScale;
uniform float u_widthScale;
uniform float u_tipWidth;
uniform vec2 u_cameraXZ;
uniform float u_shadowDistance;
uniform float u_shadowFadeBand;

out vec4 fragColor;
out vec2 fragShadowUV;

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
    position.xz += travel * plantHeight * along * u_projectionScale;
    position.xz += perpendicular * shadowWidth * across * mix(1.0, u_tipWidth, along)
                 * u_widthScale;
    vec2 worldRoot = (matModel * vec4(vertexPosition, 1.0)).xz;
    float fadeStart = max(u_shadowDistance - u_shadowFadeBand, 0.0);
    float distanceFade = 1.0 - smoothstep(fadeStart, u_shadowDistance,
                                          distance(worldRoot, u_cameraXZ));
    fragColor = vec4(vertexColor.rgb * distanceFade, vertexColor.a);
    fragShadowUV = vec2(along, across);
    gl_Position = mvp * vec4(position, 1.0);
}
