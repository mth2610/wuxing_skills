#version 330
#include "core/shaders/common/vs_header.glsl"

// Per-vertex encoded instance traits (from rlColor4ub):
//   r: paletteId / 2.0 (0.0 = Hoang Phu, 0.5 = Hoa Phu, 1.0 = Loi Phu)
//   g: glowBoost / 2.0 (0.0 .. 1.0 -> 0.0 .. 2.0)
//   b: fade alpha (0.0 .. 1.0)
//   a: unused (1.0)
in vec4 vertexColor;

uniform vec3 u_warmLightPos;
uniform vec3 u_coolLightPos;

out vec4 v_instTraits;
out vec3 v_warmLightView;
out vec3 v_coolLightView;

void main() {
    v_instTraits = vertexColor;
    // Transform world-space analytical lights through matModel (model x view under rlvk)
    // so they live in the exact same coordinate space as fragPosition and fragNormal.
    v_warmLightView = vec3(matModel * vec4(u_warmLightPos, 1.0));
    v_coolLightView = vec3(matModel * vec4(u_coolLightPos, 1.0));
    VS_FinalOutput(vertexPosition);
}
