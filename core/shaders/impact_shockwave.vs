#version 330
#include "core/shaders/common/vs_header.glsl"

// The CPU builder owns the hit shell's large-scale thickness and silhouette.
// Do not displace it here: a second shape source makes the impact unreadable.
void main()
{
    VS_FinalOutput(vertexPosition);
}
