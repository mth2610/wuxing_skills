#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

uniform float u_coreStrength; // 0 = no core, 1 = full HDR core (set by disableInnerCore)

out vec4 finalColor;

void main() {
    // Sample texture
    vec4 texColor = texture(texture0, fragTexCoord);
    
    // Calculate distance from center across the ribbon width (X coordinate)
    // fragTexCoord.x goes from 0.0 on one edge to 1.0 on the other edge.
    float centerDist = abs(fragTexCoord.x - 0.5) * 2.0;
    
    // Glow mask: soft falloff from center to edges
    float glowMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 1.25);
    
    // Smoke alpha (soft, translucent, modulated by texture)
    float smokeAlpha = texColor.a * fragColor.a * glowMask;
    
    // Core — only when u_coreStrength > 0
    vec3 finalRGB = fragColor.rgb * texColor.rgb * 1.5;
    float finalAlpha = smokeAlpha;
    
    if (u_coreStrength > 0.0)
    {
        // Core mask: tight bright filament in center
        float coreMask = pow(clamp(1.0 - centerDist, 0.0, 1.0), 5.5) * u_coreStrength;
        
        // Core alpha (opaque, independent of texture)
        float coreAlpha = clamp(fragColor.a * 1.6, 0.0, 1.0);
        
        // Blend alpha
        finalAlpha = mix(smokeAlpha, coreAlpha, coreMask);
        
        // Blend color with HDR white core
        vec3 coreColor = vec3(3.6, 3.6, 3.6);
        finalRGB = mix(finalRGB, coreColor, coreMask);
    }
    
    finalColor = vec4(finalRGB, finalAlpha);
}
