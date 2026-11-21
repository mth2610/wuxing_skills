#version 330
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

void main()
{
    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);
    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragLocalY = vertexPosition.y;

    mat3 normalMat = transpose(inverse(mat3(matModel)));
    vec3 N = normalize(normalMat * vertexNormal);
    vec3 T = normalize(normalMat * vertexTangent.xyz);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * vertexTangent.w;

    fragNormal = N;
    fragTangent = T;
    fragBitangent = B;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
