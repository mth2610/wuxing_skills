#version 330

// Đợt G2 — stylized-realism surface shader for character/prop MODELS (replaces
// raylib's UNLIT default). CPU skinning (UpdateModelAnimation) has already posed
// the mesh, so this is a plain static transform — no GPU skinning here.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexTangent; // Real Shading P5a — xyz = tangent, w = bitangent sign

// raylib auto-populates these by name (SHADER_LOC_MATRIX_*).
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 matView; // Real Shading P3c — for the matcap view-space normal

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;     // world space
out vec3 fragWorldPos;   // world space
out vec3 fragViewNormal; // view space — matcap lookup UV (P3c)
out mat3 fragTBN;        // world-space tangent basis — normal map + aniso (P5a/P5b)

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragViewNormal = normalize(mat3(matView) * fragNormal);

    vec3 T = normalize(vec3(matModel * vec4(vertexTangent.xyz, 0.0)));
    vec3 Nw = fragNormal;
    T = normalize(T - dot(T, Nw) * Nw); // Gram-Schmidt re-orthogonalize
    vec3 B = cross(Nw, T) * vertexTangent.w;
    fragTBN = mat3(T, B, Nw);

    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
