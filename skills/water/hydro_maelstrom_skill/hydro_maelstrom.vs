#version 330

// ─── Vertex Attributes ───────────────────────────────────────────
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// ─── Matrices ────────────────────────────────────────────────────
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// ─── Varyings → Fragment Shader ──────────────────────────────────
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPosition;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;

    // Transform normal into world space for correct 3D lighting
    fragNormal   = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));

    // World-space position for Fresnel + view-dependent effects
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
