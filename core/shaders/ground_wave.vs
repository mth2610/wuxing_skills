#version 330
#include "core/shaders/common/vs_header.glsl"

// The CPU mesh owns the large lip and irregular outline so it can conform to
// terrain. Keep this stage intentionally neutral: shader displacement here
// would separate the visible surface from the sampled ground.
void main()
{
    VS_FinalOutput(vertexPosition);
}
