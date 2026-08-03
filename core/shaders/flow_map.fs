#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   // Main Texture (Vị trí Slot 0 mặc định)
uniform sampler2D flowTex;    // Flow Map (Vị trí Slot 1 được bind từ C)
uniform vec4      colDiffuse;
uniform float     uTime;
uniform float     uSpeed;
uniform float     uStrength;
uniform float     uTiling;

out vec4 finalColor;

#include "core/uv/shaders/flow_map.glsl"

void main() {
    finalColor = FlowMap_SampleTwoPhase(texture0, flowTex, fragTexCoord,
                                        uTime, uSpeed, uStrength, uTiling)
                 * colDiffuse * fragColor;
}
