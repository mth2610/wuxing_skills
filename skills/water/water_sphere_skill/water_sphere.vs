#version 330
#include "core/shaders/common/vs_header.glsl"

float getJiggleWobble(vec3 pos) {
    float swell = sin(pos.y * 0.3 + u_time * 15.0);
    float bump  = cos(pos.x * 0.3 + u_time * 10.0) * sin(pos.z * 0.3 - u_time * 12.0);
    return (swell * 0.6 + bump * 0.4) * 3.0;
}

void main() {
    float wobble = getJiggleWobble(vertexPosition);
    vec3 displacedPos = vertexPosition + vertexNormal * wobble;
    VS_FinalOutput(displacedPos);
}
