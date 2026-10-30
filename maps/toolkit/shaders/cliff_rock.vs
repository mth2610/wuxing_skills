#version 330

// ============================================================
// WUXING — cliff_rock Vertex Shader
// Transforms cliff skirt mesh, passing tangent space,
// world positions, and vertex attributes (AO, moss, haze).
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec3 fragTangent;
out vec3 fragBitangent;
out vec4 fragColor;
out float fragLocalY;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragLocalY   = vertexPosition.y;

    mat3 model3 = mat3(matModel);
    mat3 normalMat = transpose(inverse(model3));
    vec3 rawN = normalMat * vertexNormal;
    float nLenSq = dot(rawN, rawN);
    fragNormal = (nLenSq > 1e-8) ? (rawN * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    vec3 rawT = model3 * vertexTangent.xyz;
    rawT -= fragNormal * dot(fragNormal, rawT);
    float tLenSq = dot(rawT, rawT);
    if (tLenSq > 1e-8) {
        fragTangent = rawT * inversesqrt(tLenSq);
    } else {
        vec3 refAxis = (abs(fragNormal.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 fallbackT = cross(refAxis, fragNormal);
        fragTangent = fallbackT * inversesqrt(max(dot(fallbackT, fallbackT), 1e-8));
    }

    float handedness = (abs(vertexTangent.w) > 0.5) ? sign(vertexTangent.w) : 1.0;
    fragBitangent = cross(fragNormal, fragTangent) * handedness;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
