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

    // Directly extract basis columns from matModel with single dot() evaluation per axis
    vec3 col0 = matModel[0].xyz;
    vec3 col1 = matModel[1].xyz;
    vec3 col2 = matModel[2].xyz;
    float lenSq0 = dot(col0, col0);
    float lenSq1 = dot(col1, col1);
    float lenSq2 = dot(col2, col2);
    fragTangent   = (lenSq0 > 1e-8) ? (col0 * inversesqrt(lenSq0)) : vec3(1.0, 0.0, 0.0);
    fragNormal    = (lenSq1 > 1e-8) ? (col1 * inversesqrt(lenSq1)) : vec3(0.0, 1.0, 0.0);
    fragBitangent = (lenSq2 > 1e-8) ? (col2 * inversesqrt(lenSq2)) : vec3(0.0, 0.0, 1.0);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}