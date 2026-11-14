#version 330
#include "core/shaders/common/vs_header.glsl"

out vec3 v_localPos;

void main() {
    v_localPos = vertexPosition;
    VS_FinalOutput(vertexPosition);
}
