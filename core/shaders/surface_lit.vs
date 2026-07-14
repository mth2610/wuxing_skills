#version 330

// Đợt G2 — stylized-realism surface shader for character/prop MODELS (replaces
// raylib's UNLIT default). CPU skinning (UpdateModelAnimation) has already posed
// the mesh, so this is a plain static transform — no GPU skinning here.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// raylib auto-populates these by name (SHADER_LOC_MATRIX_*).
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;   // world space
out vec3 fragWorldPos; // world space

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
