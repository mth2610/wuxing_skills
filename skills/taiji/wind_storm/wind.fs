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

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 3; ++i) {
        v += a * noise(p);
        p = rot * p * 2.0;
        a *= 0.5;
    }
    return v;
}

void main() {
    float localU = fragColor.r;    
    float localV = fragTexCoord.x; 
    float baseAlpha = fragColor.a;

    float shape = sin(localV * 3.14159265);
    vec2 flow = vec2(localU * 15.0 - u_time * 12.0, localV * 5.0 + u_time * 3.0);
    float n = fbm(flow);
    
    float density = shape * n * baseAlpha;
    // CẢI TIẾN: Tăng độ mượt, bớt gắt (từ mũ 1.5 xuống 1.2)
    density = pow(density, 1.2); 
    
    // Lọc kỹ hơn các phần trong suốt
    if (density < 0.05) {
        discard;
    }
    
    // CẢI TIẾN MÀU SẮC: Dịu mắt hơn, đậm chất gió bão, KHÔNG dùng trắng tinh (1.0, 1.0, 1.0)
    vec3 colorOuter = vec3(0.40, 0.65, 0.85); // Xanh lam đậm ở viền
    vec3 colorInner = vec3(0.65, 0.85, 0.95); // Xanh nhạt ở trong
    vec3 colorCore  = vec3(0.85, 0.95, 1.00); // Trắng hơi ngả xanh dương ở lõi

    vec3 mixedColor;
    
    if (density < 0.4) {
        float t = smoothstep(0.05, 0.4, density);
        mixedColor = mix(colorOuter, colorInner, t);
    } else {
        float t = smoothstep(0.4, 0.8, density);
        mixedColor = mix(colorInner, colorCore, t);
    }
    
    float verticalFade = smoothstep(0.0, 0.1, localU) * smoothstep(1.0, 0.8, localU);
    // CẢI TIẾN: Giảm hệ số nhân Alpha xuống để Additive Blend không bị chói
    float finalAlpha = clamp(density * 1.0 * verticalFade, 0.0, 1.0);
    
    finalColor = vec4(mixedColor, finalAlpha);
}