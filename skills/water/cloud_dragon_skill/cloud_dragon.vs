#version 330

// ---------------------------------------------------------------------------
//  cloud_dragon.vs — Shared vertex shader (orbs + helix tubes)
//  Passes world-space position and normal to fragment stage for
//  Fresnel, wrap lighting, and caustic effects.
//  NOTE: Raylib default VS does NOT forward fragNormal / fragPosition —
//        always load this custom VS for any 3-D lit skill mesh.
// ---------------------------------------------------------------------------

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main()
{
    // World-space position used for lighting & Fresnel in FS
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    // Transform normal with the inverse-transpose of the model matrix
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));

    fragTexCoord = vertexTexCoord;

    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
