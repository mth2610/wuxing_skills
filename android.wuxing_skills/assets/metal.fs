#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time; 

out vec4 finalColor;

// Hàm tạo mã băm ngẫu nhiên 2D
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Hàm nhiễu Value Noise 2D
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + vec2(0.0,0.0)), hash(i + vec2(1.0,0.0)), u.x),
               mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
}

// Fractal Brownian Motion 2-octave để tạo độ nhiễu cuộn ngẫu nhiên
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
    // 1. Biến dạng UV (UV Warping) để tạo hiệu ứng năng lượng kim loại lỏng lung linh cuộn chảy
    vec2 flow = vec2(u_time * 1.5, -u_time * 2.2);
    vec2 warpUV = fragTexCoord;
    
    // Nhiễu tần số cao tạo rung động kim loại lỏng nhẹ xung quanh
    float n1 = fbm(fragTexCoord * 20.0 + flow);
    warpUV.x += (n1 - 0.5) * 0.005;
    warpUV.y += (n1 - 0.5) * 0.005;

    vec4 texColor = texture(texture0, warpUV);
    float baseAlpha = texColor.a;
    
    // Tính toán độ sáng dựa trên kênh màu RGB
    float brightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    
    // Độ dày năng lượng (density) kết hợp alpha và độ sáng
    float density = baseAlpha * (0.2 + 0.8 * brightness);

    if (density < 0.01) {
        discard;
    }

    // 2. Tái tạo pháp tuyến 2D (Normal Reconstruction) từ độ dốc của kênh alpha
    // Kỹ thuật này giúp bo cong 3D cho các nét vẽ 2D
    vec2 texelSize = 1.0 / textureSize(texture0, 0);
    float dL = texture(texture0, warpUV + vec2(-texelSize.x * 2.0, 0.0)).a;
    float dR = texture(texture0, warpUV + vec2(texelSize.x * 2.0, 0.0)).a;
    float dU = texture(texture0, warpUV + vec2(0.0, -texelSize.y * 2.0)).a;
    float dD = texture(texture0, warpUV + vec2(0.0, texelSize.y * 2.0)).a;
    
    vec2 grad = vec2(dR - dL, dD - dU);
    float gradLen = length(grad);
    vec2 normal = gradLen > 0.001 ? grad / gradLen : vec2(0.0);

    // 3. Phối màu Kim Loại Lỏng (Vàng Hoàng Kim Specular & Chrome)
    vec3 goldBase   = vec3(0.72, 0.38, 0.03); // Đồng đỏ viền tối bóng
    vec3 goldMid    = vec3(1.00, 0.78, 0.08); // Vàng hoàng kim đặc trưng
    vec3 goldBright = vec3(1.00, 0.96, 0.45); // Vàng sáng phản quang
    vec3 goldWhite  = vec3(1.00, 1.00, 0.98); // Lõi trắng phản chiếu cực đại

    vec3 col;
    if (density < 0.4) {
        col = mix(goldBase, goldMid, smoothstep(0.01, 0.4, density));
    } else {
        col = mix(goldMid, goldBright, smoothstep(0.4, 0.85, density));
    }

    // Hiệu ứng ánh kim quét bề mặt Chrome (Environment Reflection)
    float envRef = sin(normal.x * 3.5 + normal.y * 2.5 + u_time * 5.0) * 0.5 + 0.5;
    col = mix(col, goldBright, envRef * 0.35 * smoothstep(0.1, 0.5, density));

    // Điểm bạo kích phản sáng (Specular Highlight) góc trên-trái chiếu xuống
    vec2 lightDir = normalize(vec2(-1.0, -1.0));
    float spec = max(dot(normal, -lightDir), 0.0);
    float specHighlight = pow(spec, 10.0) * 0.75; 
    col += vec3(specHighlight) * goldWhite;

    // Hào quang phát sáng viền ngoài (Rim Lighting)
    float rim = (1.0 - density) * gradLen * 4.5;
    col += vec3(rim * 0.45) * goldMid;

    // Vệt kiếm chớp sáng chéo dọc thân kiếm
    float gleamLine = sin(warpUV.x * 5.0 - warpUV.y * 2.5 - u_time * 8.0);
    float gleam = pow(clamp(gleamLine, 0.0, 1.0), 12.0);
    col += vec3(gleam * 0.6) * goldWhite * smoothstep(0.15, 0.6, density);

    // Bo tròn nét viền
    float alpha = smoothstep(0.01, 0.12, density);

    finalColor = vec4(col, alpha);
}