#version 330
#include "core/shaders/common/vs_header.glsl"

// Test: GPU vertex displacement — DisplaceVertex_Noise on a baked grid
// (core/procedural_mesh_utils.h's ProceduralMesh_CreateBaseGrid). noiseVal
// computed here via noise.glsl's fbm2 (displacement.glsl deliberately has
// no hard dependency on noise.glsl — see CORE_API.md).
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/displacement.glsl"

void main() {
    float noiseVal = fbm2(vertexPosition.xz * u_dispFrequency + u_time * u_dispSpeed);
    vec3 displaced = DisplaceVertex_Noise(vertexPosition, vertexNormal, noiseVal);
    VS_FinalOutput(displaced);
}
