#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

uniform float u_dissolve;
uniform vec4 u_baseColor;
uniform vec4 u_emissiveColor;
uniform sampler2D flowTex; // Vortex flow texture — bound by FlowMap_Apply via this exact name

void main() {
    // Project-standard key light (bắt buộc)
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    // UV map & FlowMap calculation để tạo cảm giác nước cuộn trào.
    // flowDir lấy từ chính flowTex (vortex texture từ FlowMap_CreateWithVortexTexture),
    // không hardcode — texture0 không được FlowMap bind (chỉ bind theo tên "flowTex").
    vec2 uv = fragTexCoord * 5.0;
    vec2 flowDir = texture(flowTex, fragTexCoord * 0.5).rg * 2.0 - 1.0;
    float flow = flowBlend(flowTex, uv, flowDir, 2.5, 0.35, u_time);
    
    // FBM Noise để tạo chi tiết gợn nước
    float noise = fbm2N(fragPosition.xz * 0.12 + vec2(u_time * 0.6), 3);
    
    // Tính toán Dissolve phase (fade lưới khi kết thúc)
    float edge;
    float burn = dissolveCalc(noise, u_dissolve, 0.15, edge);
    if (burn >= 1.0) {
        discard;
    }
    
    // Tính normal heightmap (perturbNormal)
    vec2 eps = vec2(0.1, 0.0);
    float h1 = fbm2N((fragPosition.xz - eps.xy) * 0.1, 2);
    float h2 = fbm2N((fragPosition.xz + eps.xy) * 0.1, 2);
    float h3 = fbm2N((fragPosition.xz - eps.yx) * 0.1, 2);
    float h4 = fbm2N((fragPosition.xz + eps.yx) * 0.1, 2);
    vec2 hDelta = vec2(h1 - h2, h3 - h4);
    
    // Tăng độ nảy normal bằng mix giữa FBM và flowmap displacement
    vec3 normal = perturbNormal(fragNormal, hDelta * 1.2, 0.7);
    
    // Lighting chính
    float diff = calcDiffuse(normal, lightDir, 0.25);

    // Fresnel tạo hiệu ứng bề mặt nước bao bọc. Power tăng (8.0) để rim chỉ
    // bám sát viền silhouette thật sự thay vì phủ gần như toàn bộ mặt sóng
    // cong (curling wave có rất nhiều normal gần như vuông góc camera).
    float fresnel = calcFresnel(normal, normalize(viewPos - fragPosition), 8.0);

    // Lọc dải Emissive, giới hạn khoảng 20~30% diện tích sóng sáng lên
    float emMask = smoothstep(0.78, 0.97, noise + flow * 0.45);
    // Foam/emissive nghiêng về cyan của baseColor thay vì trắng tinh để
    // không lấn át toàn bộ màu nước — chỉ điểm sáng mới ngả trắng.
    vec3 foamColor = mix(u_baseColor.rgb, u_emissiveColor.rgb, 0.55);
    vec3 emissive = foamColor * emMask * 1.1;

    // Mix các lớp màu — base diffuse luôn là lớp chiếm ưu thế, rim/emissive
    // chỉ bồi thêm một lượng giới hạn (Aesthetic Law 12.5: preserve volume).
    vec3 baseRGB = u_baseColor.rgb * (diff + 0.15);
    vec3 fresnelRGB = foamColor * fresnel * 0.35;

    vec3 finalRGB = baseRGB + emissive + fresnelRGB;
    
    // Nếu đang dissolve, mép viền sẽ phát sáng trắng
    finalRGB = mix(finalRGB, vec3(1.0, 1.0, 1.0), edge);
    
    finalColor = vec4(finalRGB, u_baseColor.a * (1.0 - edge));
}