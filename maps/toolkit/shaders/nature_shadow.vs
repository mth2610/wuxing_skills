#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;

uniform mat4 mvp;
uniform mat4 matModel;
uniform float u_time;
uniform vec2 u_windDirection;
uniform float u_windStrength;
uniform sampler2D u_interactionMap;
uniform vec2 u_interactionCenter;
uniform float u_interactionWorldSize;
uniform float u_interactionMaxBend;
uniform int u_interactionEnabled;

out vec2 fragTexCoord;

void main()
{
    vec3 local = vertexPosition;
    vec3 world = vec3(matModel * vec4(local, 1.0));
    float rootMask = vertexTexCoord.y * vertexTexCoord.y;
    // Multi-frequency wind dynamics matching nature_lit.vs
    float baseSway = sin(u_time * 0.85 + dot(world.xz, vec2(0.11, 0.08))) * 0.35;
    float windCoord = dot(world.xz, u_windDirection) * 0.42 - u_time * 1.85;
    float gustWave = pow(sin(windCoord) * 0.5 + 0.5, 2.0) * 0.85;
    float tipJitter = sin(u_time * 4.20 + dot(world.xz, vec2(0.55, -0.45)) + vertexTexCoord.x * 6.2831) * 0.12;
    float windDeflection = (baseSway + gustWave + tipJitter) * u_windStrength;
    local.xz += u_windDirection * windDeflection * rootMask;

    if (u_interactionEnabled != 0) {
        vec2 interactionUV = (world.xz - u_interactionCenter) / u_interactionWorldSize + 0.5;
        vec2 inside = step(vec2(0.0), interactionUV) * step(interactionUV, vec2(1.0));
        vec3 sampleValue = texture(u_interactionMap, clamp(interactionUV, 0.0, 1.0)).rgb;
        vec2 pushDirection = sampleValue.rg * 2.0 - 1.0;
        float interaction = sampleValue.b * inside.x * inside.y;
        local.xz += pushDirection * interaction * u_interactionMaxBend * rootMask;
    }

    fragTexCoord = vertexTexCoord2;
    gl_Position = mvp * vec4(local, 1.0);
}
