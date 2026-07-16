#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    // Sample texture
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Calculate distance from center across the ribbon width (X coordinate)
    // fragTexCoord.x goes from 0.0 on one edge to 1.0 on the other edge.
    float centerDist = abs(fragTexCoord.x - 0.5) * 2.0;
    
    // Core mask: extremely tight, bright white filament in the center (exponent 5.5)
    float coreMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 5.5);
    
    // Glow mask: soft falloff from center to edges
    float glowMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 1.25);
    
    // Smoke alpha (soft, translucent, modulated by texture)
    float smokeAlpha = texColor.a * fragColor.a * glowMask;
    
    // Core alpha (highly opaque, independent of texture transparency to keep core bright)
    float coreAlpha = clamp(fragColor.a * 1.6, 0.0, 1.0);
    
    // Smoothly blend the alphas based on coreMask
    float finalAlpha = mix(smokeAlpha, coreAlpha, coreMask);
    
    // Blend outer glow color with pure white core
    vec3 glowColor = fragColor.rgb * texColor.rgb * 1.5;
    vec3 coreColor = vec3(3.6, 3.6, 3.6); // Extreme HDR brightness for intense bloom
    
    vec3 finalRGB = mix(glowColor, coreColor, coreMask);
    
    finalColor = vec4(finalRGB, finalAlpha);
}
