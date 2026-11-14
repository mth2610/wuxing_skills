#version 330
#include "core/shaders/common/vs_header.glsl"

out vec3 v_worldPos;

void main() {
    v_worldPos = vertexPosition;
    VS_FinalOutput(vertexPosition);
}
