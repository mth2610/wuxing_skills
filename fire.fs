#version 330

in vec2 fragTexCoord;
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

// 2-Octave Fractal Brownian Motion for smoother turbulence
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    vec2 shift = vec2(100.0);
    // Rotate to reduce axial bias
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.50));
    for (int i = 0; i < 2; ++i) {
        v += a * noise(p);
        p = rot * p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

void main() {
    // Warp UV coords dynamically using FBM
    vec2 flow = vec2(u_time * 0.8, -u_time * 1.5);
    vec2 warpUV = fragTexCoord;
    
    // Add two-scale distortion
    float n1 = fbm(fragTexCoord * 6.0 + flow);
    float n2 = fbm(fragTexCoord * 15.0 - flow * 0.5);
    
    warpUV.x += (n1 - 0.5) * 0.04 + (n2 - 0.5) * 0.015;
    warpUV.y += (n1 - 0.5) * 0.03 + (n2 - 0.5) * 0.015;
    
    vec4 texColor = texture(texture0, warpUV);
    float density = texColor.r;
    
    if (density < 0.01) {
        discard;
    }
    
    // Organic tearing/dissolve at the edges using noise
    float edgeNoise = noise(fragTexCoord * 25.0 + vec2(0.0, -u_time * 4.0));
    if (density < 0.5) {
        // More erosion at the outer edges
        density = density * (1.0 - (1.0 - density * 2.0) * edgeNoise * 0.35);
    }
    
    if (density < 0.01) {
        discard;
    }

    // Fire color ramp
    vec3 darkFire   = vec3(0.45, 0.04, 0.01); // Deep crimson
    vec3 redFire    = vec3(0.85, 0.12, 0.03); // Bright red
    vec3 orangeFire = vec3(1.00, 0.45, 0.00); // Orange
    vec3 yellowFire = vec3(1.00, 0.85, 0.12); // Bright yellow
    vec3 coreFire   = vec3(1.00, 1.00, 0.95); // White hot core

    vec3 mixedColor;
    float alphaOut = smoothstep(0.01, 0.25, density); 

    if (density < 0.15) {
        float t = smoothstep(0.01, 0.15, density);
        mixedColor = mix(darkFire, redFire, t);
        alphaOut *= 0.85; 
    } else if (density < 0.40) {
        float t = smoothstep(0.15, 0.40, density);
        mixedColor = mix(redFire, orangeFire, t);
    } else if (density < 0.70) {
        float t = smoothstep(0.40, 0.70, density);
        mixedColor = mix(orangeFire, yellowFire, t);
    } else {
        float t = smoothstep(0.70, 1.00, density);
        mixedColor = mix(yellowFire, coreFire, t);
    }
    
    // Heat ripple distortion effect (uses screen size relative coord ripple)
    vec2 pixelCoords = fragTexCoord * textureSize(texture0, 0);
    float heatRipple = sin((pixelCoords.x + u_time * 4.0) * 0.15) * cos((pixelCoords.y - u_time * 10.0) * 0.15);
    
    if (density > 0.15) {
        mixedColor += vec3(heatRipple * 0.12); 
    }
    
    finalColor = vec4(mixedColor, alphaOut);
}
