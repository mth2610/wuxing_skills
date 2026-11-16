#version 330
#include "core/shaders/common/vs_header.glsl"

in vec4 vertexColor;

out vec3 v_worldPos;
out vec4 fragColor;

void main() {
    v_worldPos = vertexPosition;
    fragColor = vertexColor;
    VS_FinalOutput(vertexPosition);
}
