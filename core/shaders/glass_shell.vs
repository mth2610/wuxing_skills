#version 330
#include "core/shaders/common/vs_header.glsl"

out vec3 shieldViewDir;
out float shieldFresnel;

// DrawCoreSphere is immediate-mode geometry. BeginMode3D has already applied
// the model-view transform on the CPU, so both attributes arrive in view space.
// Calling VS_FinalOutput here would apply matModel a second time and skew the
// normal, producing a crescent across one hemisphere instead of a silhouette rim.
void main() {
    fragPosition = vertexPosition;
    fragNormal = normalize(vertexNormal);
    fragTexCoord = vertexTexCoord;
    shieldViewDir = normalize(-vertexPosition);
    float fresnelM = 1.0 - clamp(dot(fragNormal, shieldViewDir), 0.0, 1.0);
    float fresnelX2 = fresnelM * fresnelM;
    shieldFresnel = fresnelX2 * fresnelX2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
