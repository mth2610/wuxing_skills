#version 330

// ============================================================
// WUXING — prop_lit Vertex Shader (CORE_ISSUES.md Item 36)
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec3 fragNormal;
out float fragLocalY;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragLocalY   = vertexPosition.y;

    mat3 model3 = mat3(matModel);
    mat3 normalMat = mat3(
        cross(model3[1], model3[2]),
        cross(model3[2], model3[0]),
        cross(model3[0], model3[1])
    );
    vec3 rawN = normalMat * vertexNormal;
    float nLenSq = dot(rawN, rawN);
    fragNormal = (nLenSq > 1e-8) ? (rawN * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
