#version 330
#include "core/shaders/common/vs_header.glsl"

// Volume campfire — the fragment shader raymarches the fire inside the bounding proxy sphere;
// this VS only needs the standard engine output (fragPosition + fragNormal for ray reconstruction).
void main()
{
    VS_FinalOutput(vertexPosition);
}
