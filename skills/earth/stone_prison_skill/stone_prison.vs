#version 330
#include "core/shaders/common/vs_header.glsl"

// vertexColor/fragColor not covered by vs_header.glsl (only skills needing
// vertex color declare it themselves) — stone_prison.fs multiplies by it.
in vec4 vertexColor;
out vec4 fragColor;

void main()
{
    fragColor = vertexColor;
    VS_FinalOutput(vertexPosition);
}
