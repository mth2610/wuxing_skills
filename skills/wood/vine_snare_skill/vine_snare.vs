#version 330
#include "core/shaders/common/vs_header.glsl"
#include "core/shaders/common/noise.glsl"

void main() {
    vec3 pos = vertexPosition;
    
    // Add subtle, slow ambient wind/writhing to the vines based on world height
    float writhe = fbm2(vec2(pos.y * 0.05, u_time * 0.5));
    pos.x += writhe * 1.5;
    pos.z += writhe * 1.5;
    
    VS_FinalOutput(pos);
}