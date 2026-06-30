#version 330
#include "core/shaders/common/vs_header.glsl"
#include "core/shaders/common/noise.glsl"

// Bark deformation driven by FBM — keep this height-field identical in the .fs
// so the re-derived normal matches the displaced surface (see perturbNormal).
uniform float u_dissolve;   // 0 = alive, 1 = fully dried/dissolved

float barkHeight(vec2 uv) {
    // Gentle ridges running down the length — subtle, not jagged.
    float ridges = fbm2N(vec2(uv.x * 4.0, uv.y * 2.0), 2);
    return ridges;
}

void main() {
    vec2 uv = vertexTexCoord;

    // Mild bark relief, just enough to break up a perfectly smooth tube.
    float h = barkHeight(uv);
    float disp = (h - 0.5) * (1.0 - u_dissolve * 0.5);

    vec3 displaced = vertexPosition + vertexNormal * (disp * 0.6);
    VS_FinalOutput(displaced);
}
