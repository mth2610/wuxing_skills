#version 330

// Đợt E / F1 — lit CPU particles.
//
// Deliberately does NOT include common/vs_header.glsl: that header declares the
// mesh attribute set (vertexNormal, matModel), and these vertices come from
// rlgl's immediate-mode batch, which supplies position + texcoord + color only.
// Attribute names below must match rlgl's default batch bindings.
//
// rlgl emits batch vertices already in world space and keeps matModel at
// identity, so fragPosition is the world position with no transform needed —
// see ENGINE_LANDMINES.md on the matModel-identity rule.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragPosition = vertexPosition;
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
