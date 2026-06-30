#version 330
#include "core/shaders/common/vs_header.glsl"

// Test: GPU vertex displacement — DisplaceVertex_TwistAndTaper on a baked
// cylinder, stationary (no path bend). Unlike DisplaceVertex_AlongPath
// (which already outputs world-space positions from world-space path
// control points), this function stays in local space — matModel handles
// world placement/scale normally (matModel here scales local height
// [0,1] -> real height and translates to the test column's position).
#include "core/shaders/common/displacement.glsl"

void main() {
    vec3 displaced = DisplaceVertex_TwistAndTaper(vertexPosition);
    VS_FinalOutput(displaced);
    // VS_FinalOutput computed fragNormal from the un-rotated vertexNormal —
    // override with the twist-aware normal so lighting matches the swirl.
    fragNormal = normalize(vec3(matModel * vec4(
        DisplaceVertex_TwistAndTaperNormal(vertexPosition, vertexNormal), 0.0)));
}
