#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"

// ============================================================
// Water Splash Material (Fragment Shader)
// ============================================================

uniform sampler2D texture0;         
uniform vec4  u_baseColor;          
uniform float u_translucency;
uniform float u_dissolve;           
uniform float u_rimStrength;
uniform float u_fresnelPower;
uniform float u_emissiveIntensity;

uniform float u_customParam1; 

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5)); 
    vec3 viewDir = normalize(cameraPos - fragPosition);
    vec3 normal = normalize(fragNormal);

    // --- 1. HIỆU ỨNG TAN BIẾN (DISSOLVE / MIST) ---
    // Vì mesh đã rách sẵn, ta dùng noise để làm nó dần tan biến ở cuối đời
    vec2 uv = fragTexCoord + vec2(0.0, u_customParam1 * 0.5);
    float noiseVal = texture(texture0, uv * 3.0).r;
    
    // Chỉ bắt đầu tan biến khi progress > 0.5 (nước đang rơi xuống)
    float dissolveProgress = smoothstep(0.5, 1.0, u_customParam1);
    
    // Alpha mask "ăn mòn" bề mặt nước theo hình thù của noise map
    float alphaMask = smoothstep(dissolveProgress - 0.1, dissolveProgress + 0.1, noiseVal);
    
    if (alphaMask < 0.05 && dissolveProgress > 0.0) {
        discard;
    }

    // --- 2. ÁNH SÁNG & ĐỘ BÓNG (LIGHTING & SPECULAR) ---
    float diffuse = calcDiffuse(normal, lightDir, 0.2);
    float fresnel = calcFresnel(normal, viewDir, u_fresnelPower);

    // Nước cần độ lóa (Specular Highlight) cực mạnh để trông ướt át
    vec3 halfVector = normalize(lightDir + viewDir);
    float specAmount = pow(max(dot(normal, halfVector), 0.0), 128.0) * 2.0;

    vec3 baseColor = u_baseColor.rgb * diffuse;
    
    // Viền nước đổi màu theo Fresnel
    baseColor += vec3(fresnel) * u_rimStrength * u_baseColor.rgb; 
    
    // Thêm Specular (Sẽ mờ dần khi giọt nước tan biến để tránh bị đốm sáng bay lơ lửng)
    baseColor += vec3(1.0) * specAmount * alphaMask;

    // --- 3. ĐIỂM NHẤN Ở CÁC HẠT NƯỚC ĐANG TAN BIẾN ---
    // Tạo viền sáng ở mép nước đang bốc hơi/rớt xuống
    float edgeHighlight = smoothstep(dissolveProgress, dissolveProgress + 0.05, noiseVal);
    baseColor += vec3(1.0) * (1.0 - edgeHighlight) * dissolveProgress;

    // Fade mờ toàn bộ ở 20% cuối cùng của vòng đời để kết thúc êm ái
    float globalFade = 1.0 - smoothstep(0.8, 1.0, u_customParam1);
    float finalAlpha = u_baseColor.a * u_translucency * alphaMask * globalFade;
    
    FS_FinalOutput(vec4(baseColor, finalAlpha));
}