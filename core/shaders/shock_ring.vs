#version 330
#include "core/shaders/common/vs_header.glsl"

// The CPU sweep owns the ring's radius, its band and its lens section. The
// silhouette — which parts of that band are actually THERE — is the fragment
// stage's job, so nothing is displaced here: two shape sources on one ring make
// the fibres slide against the geometry they are supposed to be made of.
void main()
{
    VS_FinalOutput(vertexPosition);
}
