#version 330
#include "core/shaders/common/vs_header.glsl"

// Wuxia Unified Shield — Ground-Contact Floor Caustic Vertex Shader
// Immediate-mode RL_QUADS inside BeginMode3D arrives in view space;
// vertexTexCoord carries [0..1] quad UV so fragment shader evaluates radial/azimuthal patterns.

out vec2 vGroundUV;

void main()
{
    vGroundUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
