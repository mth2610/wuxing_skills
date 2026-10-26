#version 330

// ============================================================
// WUXING — prop_lit Vertex Shader (CORE_ISSUES.md Item 36)
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;   // xyz = tangent, w = bitangent handedness sign

uniform mat4 mvp;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec3 fragTangent;
out vec3 fragBitangent;
out float fragLocalY;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragLocalY   = vertexPosition.y;

    mat3 normalMat = mat3(matModel);
    vec3 rawN = normalMat * vertexNormal;
    float nLenSq = dot(rawN, rawN);
    fragNormal = (nLenSq > 1e-8) ? (rawN * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    // [BUG FIX]: Bảo vệ chống lỗi normalize(vec3(0.0)) -> NaN nếu mesh quên gọi GenMeshTangents()
    // hoặc khi vertexTangent song song với vertexNormal
    vec3 rawT = normalMat * vertexTangent.xyz;
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
