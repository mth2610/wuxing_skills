#version 330

// Đợt E / E2 — added so ground_splat.fs can be lit by VFX point lights.
//
// The ground previously ran on raylib's DEFAULT vertex shader, which forwards
// only texcoord and colour — no world position. Point lighting needs one, and
// there is no way to recover it from a UV alone once the plane is scaled or
// offset (the heightmap variant draws at a non-zero drawOffset). So this is the
// default shader plus a single extra varying; everything else is unchanged, and
// the fragment shader's existing inputs keep their meaning.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec4 fragColor;
// DrawMesh under this project's custom 3D pass includes view in matModel.
// This is therefore the shared surface/view space used by VFX lights; the
// shadow uploader folds inverse(view) into its light projection.
out vec3 fragPosition;
out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal   = normalize(mat3(matModel) * vertexNormal);
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
