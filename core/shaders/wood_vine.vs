#version 330
#ifdef INSTANCED
#include "core/shaders/common/vs_instanced_header.glsl"
#else
#include "core/shaders/common/vs_header.glsl"
#endif
#include "core/shaders/common/wood_fx.glsl"

uniform float u_growth;
uniform float u_birth;
uniform float u_span;
uniform float u_tipLen;
uniform float u_swayAmp;

void main() {
    float arc = vertexTexCoord.y;
    float birth = (u_span > 0.001) ? u_birth : 0.0;
    float span = (u_span > 0.001) ? u_span : 1.0;
    float tipLen = (u_tipLen > 0.001) ? u_tipLen : 0.08;
    float growth = (u_growth >= 0.0) ? u_growth : 1.0;

    // 1. Organic growth factor along vine arc
    float g = Wood_GrowthFactor(arc, birth, span, growth, tipLen);

    // 2. Sway offset with mass inertia
    vec3 sway = Wood_SwayOffset(vertexPosition, arc, u_time, u_swayAmp);

    // 3. Tip contraction: collapse toward centerline under growth
    // For immediate/pre-baked tube, vertexNormal points outward radially
    float nominalRadiusScale = g * g;
    vec3 localDisplaced = vertexPosition + sway;

    VS_FinalOutput(localDisplaced);
}
