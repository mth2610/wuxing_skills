#version 330

// Đợt E / E2 — added so path_blend.fs can be lit by VFX point lights.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
// Project surface/view space; MapShadow_UpdateShader compensates its light VP.
out vec3 fragPosition;

// TBN vectors derived purely from uniform matModel are constant across the draw call.
// Using 'flat' saves barycentric interpolation across every fragment and avoids re-normalizing in FS.
flat out vec3 fragTangent;
flat out vec3 fragBitangent;
flat out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    // Directly extract basis columns from matModel instead of 3 full mat3 * vec3 multiplications
    fragTangent   = normalize(matModel[0].xyz);
    fragNormal    = normalize(matModel[1].xyz);
    fragBitangent = normalize(matModel[2].xyz);

    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}