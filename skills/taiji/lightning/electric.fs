#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform float u_time; 

out vec4 finalColor;

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
    // Trích xuất tọa độ uốn lượn trực tiếp từ dải Ribbon 3D
    float localU = fragColor.r;    // Dọc chiều dài tia sét
    float localV = fragTexCoord.x; // Ngang bề rộng tia sét (0.0 -> 1.0)
    float baseAlpha = fragColor.a;

    vec2 flow = vec2(u_time * 12.0, -u_time * 16.0);
    vec2 warpUV = vec2(localU, localV);
    
    // Add multi-scale noise warping
    float n1 = fbm(vec2(localU, localV) * 12.0 + flow);
    float n2 = fbm(vec2(localU, localV) * 24.0 - flow * 0.4);
    
    warpUV.x += (n1 - 0.5) * 0.05;
    warpUV.y += (n2 - 0.5) * 0.05;
    
    // Tính toán mật độ Plasma (Sáng trắng ở lõi, nhạt dần ra viền)
    float distToCenter = abs(warpUV.y - 0.5) * 2.0;
    float shape = smoothstep(1.0, 0.2, distToCenter);
    float density = shape * baseAlpha;
    
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
    
    // High-frequency amplitude flickering (Chớp giật điện áp liên tục)
    float flicker = noise(vec2(u_time * 50.0, localU * 10.0)) * 0.15 + 0.92;
    mixedColor *= flicker;
    
    // Nứt hồ quang nhỏ (Crackling filaments) nằm sâu trong lõi Plasma
    if (density > 0.25) {
        float filament = sin((warpUV.x * 60.0 + u_time * 8.0)) * cos((warpUV.y * 60.0 - u_time * 12.0));
        filament = pow(clamp(filament, 0.0, 1.0), 4.0);
        mixedColor += vec3(filament * 0.45) * cyanGlow * flicker;
    }
    
    // Hào quang mềm tỏa ra quanh lõi sét
    float glow = smoothstep(0.0, 0.4, density) * 0.45;
    mixedColor += vec3(glow * 0.8, glow * 1.5, glow * 2.2);
    alphaOut += glow * 0.5;
    
    finalColor = vec4(mixedColor, clamp(alphaOut, 0.0, 1.0));
}