#version 330
#include "core/shaders/common/vs_header.glsl"

// The CPU submits ONE flat quad and nothing else. Every ring, glyph, dash and
// square in this effect is a fragment-stage computation over that quad's local
// coordinate, which arrives in fragTexCoord already expressed in RUNE RADII
// (|uv| == 1.0 is the nominal ring). Nothing is displaced here.
//
// fragPosition is NOT used by the fragment stage and must not be: matModel is
// model x view for every draw inside MyBeginMode3D (ENGINE_LANDMINES 9), so it
// would hand the shader view space while reading like world space. The texture
// coordinate is the only channel that carries the disc's own frame.
void main()
{
    VS_FinalOutput(vertexPosition);
}
