#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time; 

out vec4 finalColor;

// 2D Pseudo-Random Noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + vec2(0.0,0.0)), hash(i + vec2(1.0,0.0)), u.x),
               mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
}

// 3-Octave Fractional Brownian Motion for plasma/lightning fractal distortion
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 3; ++i) {
        v += a * noise(p);
        p = rot * p * 2.5;
        a *= 0.5;
    }
    return v;
}

void main() {
    // 1. Dynamic coordinate distortion to simulate plasma energy flowing and crackling
    vec2 flow = vec2(u_time * 12.0, -u_time * 16.0);
    vec2 warpUV = fragTexCoord;
    
    // Add multi-scale noise warping
    float n1 = fbm(fragTexCoord * 12.0 + flow);
    float n2 = fbm(fragTexCoord * 24.0 - flow * 0.4);
    warpUV.x += (n1 - 0.5) * 0.015 + (n2 - 0.5) * 0.005;
    warpUV.y += (n1 - 0.5) * 0.015 + (n2 - 0.5) * 0.005;
    
    vec4 texColor = texture(texture0, warpUV);
    float density = texColor.a;
    
    if (density < 0.015) {
        discard;
    }
    
    // Electric plasma color ramp:
    // Core (almost white) -> Bright Cyan -> Electric Blue -> Deep Violet/Purple
    vec3 deepPurple = vec3(0.40, 0.05, 0.90);
    vec3 blueGlow   = vec3(0.05, 0.35, 1.00);
    vec3 cyanGlow   = vec3(0.15, 0.80, 1.00);
    vec3 whiteCore  = vec3(0.95, 0.98, 1.00);
    
    vec3 mixedColor;
    float alphaOut = smoothstep(0.015, 0.25, density);
    
    if (density < 0.20) {
        float t = smoothstep(0.015, 0.20, density);
        mixedColor = mix(deepPurple, blueGlow, t);
        alphaOut *= 0.85;
    } else if (density < 0.55) {
        float t = smoothstep(0.20, 0.55, density);
        mixedColor = mix(blueGlow, cyanGlow, t);
    } else if (density < 0.85) {
        float t = smoothstep(0.55, 0.85, density);
        mixedColor = mix(cyanGlow, whiteCore, t);
    } else {
        mixedColor = whiteCore;
    }
    
    // 2. High-frequency amplitude flickering representing unstable voltage/current
    float flicker = noise(vec2(u_time * 50.0, 0.0)) * 0.15 + 0.92;
    mixedColor *= flicker;
    
    // 3. Crackling filament lines within dense plasma regions (e.g. plasma orb center)
    if (density > 0.25) {
        float filament = sin((warpUV.x * 60.0 + u_time * 8.0)) * cos((warpUV.y * 60.0 - u_time * 12.0));
        filament = pow(clamp(filament, 0.0, 1.0), 4.0);
        // Add crackling sparks inside the core
        mixedColor += vec3(filament * 0.45) * cyanGlow * flicker;
    }
    
    // 4. Soft halo glow around plasma filaments
    float glow = smoothstep(0.0, 0.4, density) * 0.45;
    mixedColor += vec3(glow * 0.8, glow * 1.5, glow * 2.2); // bloom cyan/blue light bleeding
    alphaOut += glow * 0.5;
    
    finalColor = vec4(mixedColor, clamp(alphaOut, 0.0, 1.0));
}
