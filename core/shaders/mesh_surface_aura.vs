#version 330
#include "core/shaders/common/vs_header.glsl"
// A small outward shell avoids coplanar depth rejection against the source
// model while remaining below the visible silhouette expansion of the rim.
void main() { VS_FinalOutput(vertexPosition + vertexNormal * 0.012); }
