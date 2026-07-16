#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0; // Texture đường
uniform vec2 tiling;
uniform vec4 colDiffuse;

// Thông số ánh sáng
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

// --- HÀM TẠO NHIỄU (Value Noise) ---
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float noise(vec2 st) {
    vec2 i = floor(st); vec2 f = fract(st);
    float a = random(i); float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0)); float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main()
{
    // Đọc màu texture lặp lại của con đường
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, tiledUV);

    // --- TẠO LỀ ĐƯỜNG MỜ & MÉO MÓ ---
    // Dùng nhiễu (noise) để phá vỡ đường thẳng hình học của Mesh
    float n = noise(tiledUV * 2.0) * 0.1;
    
    // fragTexCoord.y đại diện cho chiều rộng của con đường (từ lề trái 0.0 đến lề phải 1.0)
    float edgeV = fragTexCoord.y + (n - 0.1); 

    // Smoothstep: Làm mờ 25% diện tích ở hai bên lề đường
    float alpha = smoothstep(0.0, 0.25, edgeV) * smoothstep(1.0, 0.75, edgeV);

    // Khử Z-fighting: Cắt bỏ luôn các điểm ảnh quá trong suốt để tránh nhấp nháy với mặt cỏ
    if (alpha < 0.1) discard; 

    // --- ÁNH SÁNG ---
    vec3 normal = vec3(0.0, 1.0, 0.0);
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) light = normalize(-lightDir);
    float NdotL = max(dot(normal, light), 0.0);
    
    vec4 actualAmbient = ambientColor.a == 0.0 ? vec4(0.4, 0.4, 0.4, 1.0) : ambientColor;
    vec4 actualLight = lightColor.a == 0.0 ? vec4(1.0, 1.0, 1.0, 1.0) : lightColor;
    vec4 totalLight = actualAmbient + (actualLight * NdotL);

    // Alpha KHÔNG được nhân totalLight.a (tới ~2): trên scene buffer HDR float
    // (Đợt G) src alpha không bị kẹp [0,1] → blend hoá điên, biển mây lòi qua
    // đường. Alpha chỉ = texture.a * tint.a * độ-mờ-lề (đều ≤1); lit chỉ ở rgb.
    finalColor = vec4((texColor * colDiffuse * totalLight).rgb,
                      texColor.a * colDiffuse.a * alpha);
}