#version 330
#include "core/shaders/common/vs_header.glsl"

// Nothing displaced, nothing clever. VS_FinalOutput is the engine's own
// convention for fragPosition/fragNormal, and testing anything else would
// answer a question nobody asked.
void main() {
    VS_FinalOutput(vertexPosition);
}
