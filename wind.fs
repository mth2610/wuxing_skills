#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time; 

out vec4 finalColor;

void main() {
    float density = texture(texture0, fragTexCoord).a;
    
    if (density < 0.02) {
        discard;
    }
    
    // Wind color ramp: Pale white/gray to soft cyan
    vec3 colorOuter = vec3(0.65, 0.85, 0.95); // Cyan-gray tint (Outer)
    vec3 colorInner = vec3(0.9, 0.95, 1.0);   // Almost pure white (Inner)
    vec3 colorCore  = vec3(1.0, 1.0, 1.0);   // Pure white core

    vec3 mixedColor;
    
    if (density < 0.5) {
        float t = smoothstep(0.02, 0.5, density);
        mixedColor = mix(colorOuter, colorInner, t);
    } else {
        float t = smoothstep(0.5, 0.9, density);
        mixedColor = mix(colorInner, colorCore, t);
    }
    
    // Swirling flow noise
    vec2 flow = vec2(u_time * 10.0, u_time * -6.0);
    vec2 pixelCoords = fragTexCoord * textureSize(texture0, 0);
    
    // Wave ripples representing air currents
    float wave = sin((pixelCoords.x + flow.x) * 0.15) * cos((pixelCoords.y + flow.y) * 0.15);
    wave = clamp(wave, -1.0, 1.0);
    
    mixedColor += vec3(wave * 0.08); // Subtle wind turbulence lighting
    
    float alpha = smoothstep(0.02, 0.3, density) * 0.75; // Cap maximum transparency
    
    finalColor = vec4(mixedColor, alpha);
}
