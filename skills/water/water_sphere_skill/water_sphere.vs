#version 330
#include "core/shaders/common/vs_header.glsl"

float getJiggleWobble(vec3 pos) {
    // Spatial frequencies ×100 (world-space: pos now in metres, was cm).
    // Amplitude ÷100: 3.0 cm → 0.03 m.
    float swell = sin(pos.y * 30.0 + u_time * 15.0);
    float bump  = cos(pos.x * 30.0 + u_time * 10.0) * sin(pos.z * 30.0 - u_time * 12.0);
    return (swell * 0.6 + bump * 0.4) * 0.03;
}

void main() {
    float wobble = getJiggleWobble(vertexPosition);
    vec3 displacedPos = vertexPosition + vertexNormal * wobble;
    VS_FinalOutput(displacedPos);
}
