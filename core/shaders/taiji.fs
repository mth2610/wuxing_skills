#version 330
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Standard NTSC/BT.601 conversion to grayscale
    float gray = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    
    // Retain a tiny bit of blue/purple tint for aesthetic moonlit look
    vec3 grayscaleColor = vec3(gray);
    grayscaleColor.b *= 1.05; // Slightly cool tint
    grayscaleColor.r *= 0.95;
    
    finalColor = VFX_ResolveBody(grayscaleColor, 1.0, texColor.a);
}
