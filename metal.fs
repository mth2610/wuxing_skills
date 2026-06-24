#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
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
    for (int i = 0; i < 2; ++i) {
        v += a * noise(p);
        p = rot * p * 2.0;
        a *= 0.5;
    }
    return v;
}

void main() {
    float elementType = fragColor.b;
    float baseAlpha = fragColor.a;

    float density = 0.0;
    float normalVal = 0.0;

    // 1. RIBBON TRAIL (Vệt kiếm bay)
    if (elementType < 0.2) {
        float localV = fragTexCoord.x;
        float localU = fragColor.r;
        float shape = sin(localV * 3.14159265);
        
        vec2 flow = vec2(u_time * 2.0, -u_time * 1.5);
        float n = fbm(vec2(localU, localV) * 15.0 + flow);
        
        density = baseAlpha * shape * (0.5 + 0.5 * n);
        normalVal = shape;
    } 
    // 2. SWORD SPRITE (Lưỡi kiếm 3D)
    else if (elementType < 0.7) {
        vec2 flow = vec2(u_time * 1.5, -u_time * 2.2);
        vec2 warpUV = fragTexCoord;
        warpUV += (fbm(fragTexCoord * 20.0 + flow) - 0.5) * 0.01;

        vec4 tex = texture(texture0, warpUV);
        density = tex.a * baseAlpha;
        normalVal = tex.a;
    } 
    // 3. PORTAL (Cổng dịch chuyển cuộn xoáy)
    else {
        vec2 uv = fragTexCoord - vec2(0.5);
        float dist = length(uv) * 2.0;
        float angle = atan(uv.y, uv.x);
        
        float ring = smoothstep(0.95, 0.8, dist) * smoothstep(0.6, 0.8, dist);
        float spiral = sin(angle * 5.0 - dist * 8.0 - u_time * 10.0) * 0.5 + 0.5;
        float glow = smoothstep(1.0, 0.0, dist) * 0.4;
        
        density = baseAlpha * (ring + spiral * glow);
        normalVal = ring + spiral * 0.5;
    }

    // Cắt bỏ phần trong suốt để tối ưu Fillrate
    if (density < 0.01) discard;

    // Phối màu Kim Loại Lỏng
    vec3 goldBase   = vec3(0.72, 0.38, 0.03); // Đồng đỏ viền tối
    vec3 goldMid    = vec3(1.00, 0.78, 0.08); // Vàng hoàng kim đặc trưng
    vec3 goldBright = vec3(1.00, 0.96, 0.45); // Vàng sáng phản quang
    vec3 goldWhite  = vec3(1.00, 1.00, 0.98); // Lõi trắng phản chiếu cực đại

    vec3 col = mix(goldBase, goldMid, smoothstep(0.01, 0.4, density));
    col = mix(col, goldBright, smoothstep(0.4, 0.85, density));

    // Phản quang bề mặt môi trường (Environment Reflection)
    float envRef = sin(normalVal * 6.0 + u_time * 8.0) * 0.5 + 0.5;
    col = mix(col, goldBright, envRef * 0.45 * smoothstep(0.1, 0.5, density));

    // Điểm bạo kích phản sáng (Specular Highlight)
    float spec = pow(clamp(normalVal, 0.0, 1.0), 6.0);
    col += vec3(spec * 0.85) * goldWhite * baseAlpha;

    finalColor = vec4(col, clamp(density, 0.0, 1.0));
}