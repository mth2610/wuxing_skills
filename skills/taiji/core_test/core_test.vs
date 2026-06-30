#version 330
#include "core/shaders/common/vs_header.glsl"

// Test: GPU vertex displacement (core/procedural_mesh_utils.h CreateBaseCylinder
// + displacement.glsl's DisplaceVertex_AlongPath) — bake-once cylinder bent
// every frame by the vertex shader, no CPU rebuild.
#include "core/shaders/common/displacement.glsl"

void main() {
    vec3 displaced = DisplaceVertex_AlongPath(vertexPosition, vertexTexCoord);
    VS_FinalOutput(displaced);
    // VS_FinalOutput computed fragNormal from the un-rotated vertexNormal —
    // override with the frame-aware normal so lighting matches the bend.
    fragNormal = normalize(vec3(matModel * vec4(
        DisplaceVertex_AlongPathNormal(vertexNormal, vertexTexCoord), 0.0)));
}
