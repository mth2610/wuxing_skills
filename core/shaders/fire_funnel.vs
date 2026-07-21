#version 330
#include "core/shaders/common/vs_header.glsl"

uniform float u_distortionStrength;

void main() {
    float h = vertexTexCoord.y;
    float u = vertexTexCoord.x;

    float w1 = sin(u * 4.0 + h * 3.0 + u_time * 2.0);
    float w2 = cos(u * 3.0 + h * 4.0 - u_time * 2.5);
    float w3 = sin(h * 5.0 + u_time * 3.0);

    float flame = w1 * 0.4 + w2 * 0.35 + w3 * 0.25;
    float amp = h * h * u_distortionStrength;

    vec3 radial = vertexNormal * flame * amp;
    float yDisp = (w3 * 0.5 + w2 * 0.3) * amp * 0.6;

    VS_FinalOutput(vertexPosition + radial + vec3(0.0, yDisp, 0.0));
}
