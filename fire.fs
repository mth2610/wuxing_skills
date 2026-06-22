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

void main() {
    float circleAlpha = texture(texture0, fragTexCoord).r;
    if (circleAlpha < 0.05) {
        finalColor = vec4(0.0);
        return;
    }

    vec2 warpUV = fragTexCoord;
    
    // Gió tạt ngang, xé rách UV nhẹ nhàng
    warpUV.x += sin(warpUV.y * 12.0 - u_time * 8.0) * 0.06;
    
    // Cuộn UV từ dưới lên trên để tạo hiệu ứng ngọn lửa bốc lên
    vec2 flow = vec2(0.0, -u_time * 3.5);
    
    float n = noise(warpUV * 4.0 + flow);
    n += noise(warpUV * 8.0 - flow * 0.5) * 0.5;
    
    float density = circleAlpha * n * fragColor.r * 2.5;
    
    vec3 darkFire   = vec3(0.60, 0.10, 0.02);
    vec3 orangeFire = vec3(1.00, 0.40, 0.00);
    vec3 coreFire   = vec3(1.00, 0.90, 0.40);

    vec3 mixedColor;
    float alphaOut = smoothstep(0.05, 0.4, density); 

float safeDensity = clamp(density, 0.0, 1.2);

    // Tính toán tỷ lệ phân tầng bằng hiệu số tuyến tính (Thay cho smoothstep lỗi biên)
    float t1 = clamp((safeDensity - 0.3) / 0.4, 0.0, 1.0); // Phân tầng từ 0.3 -> 0.7 (độ rộng 0.4)
    float t2 = clamp((safeDensity - 0.7) / 0.5, 0.0, 1.0); // Phân tầng từ 0.7 -> 1.2 (độ rộng 0.5)

    // Hòa trộn màu liên tục, song song trên GPU không cần rẽ nhánh
    vec3 col0 = mix(darkFire, orangeFire, t1);
    mixedColor = mix(col0, coreFire, t2);
    
    finalColor = vec4(mixedColor, alphaOut * fragColor.a);
}