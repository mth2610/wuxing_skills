#version 330

// Đợt E / E2 — added so path_blend.fs can be lit by VFX point lights.
//
// The ground previously ran on raylib's DEFAULT vertex shader, which forwards
// only texcoord and colour — no world position. Point lighting needs one, and
// there is no way to recover it from a UV alone once the plane is scaled or
// offset (the heightmap variant draws at a non-zero drawOffset). So this is the
// default shader plus a single extra varying; everything else is unchanged, and
// the fragment shader's existing inputs keep their meaning.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec4 fragColor;
// Project surface/view space; MapShadow_UpdateShader compensates its light VP.
out vec3 fragPosition;
out vec3 fragTangent;
out vec3 fragBitangent;
out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTangent = normalize(mat3(matModel) * vec3(1.0, 0.0, 0.0));
    fragBitangent = normalize(mat3(matModel) * vec3(0.0, 0.0, 1.0));
    fragNormal = normalize(mat3(matModel) * vec3(0.0, 1.0, 0.0));
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
