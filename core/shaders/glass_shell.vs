#version 330
#include "core/shaders/common/vs_header.glsl"

// DrawCoreSphere is immediate-mode geometry. BeginMode3D has already applied
// the model-view transform on the CPU, so both attributes arrive in view space.
// Calling VS_FinalOutput here would apply matModel a second time and skew the
// normal, producing a crescent across one hemisphere instead of a silhouette rim.
void main() {
    fragPosition = vertexPosition;
    fragNormal = normalize(vertexNormal);
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
