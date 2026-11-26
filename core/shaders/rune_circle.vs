#version 330
#include "core/shaders/common/vs_header.glsl"

in vec4 vertexColor;
out vec4 fragColor;

// Vertex shader for Cổ Ấn Vạn Pháp (VFX_ComposeRuneCircle / Variant).
// Surfaces arrive with fragTexCoord in [0, 1] (for rune/pulse quads) or [0, 1]
// per billboard (for pinpoint embers, with fragColor carrying heat/alpha).
void main()
{
    fragColor = vertexColor;
    VS_FinalOutput(vertexPosition);
}
