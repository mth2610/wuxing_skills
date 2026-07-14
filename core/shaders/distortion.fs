#version 330

// Input attributes from vertex shader
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniforms
uniform sampler2D texture0; // Cảnh nền đã render (RenderTexture)
uniform vec4 colDiffuse;

// Danh sách các nguồn biến dạng màn hình (tối đa 8 nguồn đồng thời)
uniform vec2 u_centers[8];    // Vị trí tâm dạng chuẩn hóa screen-space [0.0 .. 1.0]
uniform float u_radii[8];     // Bán kính vùng ảnh hưởng lớn nhất
uniform float u_strengths[8]; // Cường độ biến dạng (chiết suất khúc xạ)
uniform float u_progress[8];  // Tiến trình thời gian sống [0.0 .. 1.0]
uniform int u_count;          // Số nguồn đang hoạt động
uniform float u_aspectRatio;  // Tỉ lệ khung hình (width/height) để khử bóp méo elip

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    vec2 totalOffset = vec2(0.0);

    for (int i = 0; i < u_count; i++) {
        // Điều chỉnh toạ độ y theo aspect ratio để tính toán khoảng cách tròn đều (khử bóp méo elip)
        vec2 diff = uv - u_centers[i];
        diff.y /= u_aspectRatio;
        
        float dist = length(diff);
        float currentRadius = u_radii[i] * u_progress[i]; // Bán kính sóng lan rộng dần theo thời gian
        
        // Độ dày của rìa sóng xung kích ( shockwave ring width )
        float ringWidth = u_radii[i] * 0.15; 
        
        if (dist < currentRadius && dist > currentRadius - ringWidth) {
            // Tính toán vị trí pixel tương đối bên trong dải độ dày sóng [-1.0 .. 1.0]
            float mid = currentRadius - ringWidth * 0.5;
            float x = (dist - mid) / (ringWidth * 0.5); 
            
            // Cấu trúc sóng lõm-lồi hình sin khúc xạ
            float wave = sin(x * 3.14159265);
            
            // Giảm dần biên độ ở rìa sóng và khi sắp biến mất (progress -> 1.0)
            float fade = (1.0 - u_progress[i]) * (1.0 - abs(x));
            
            // Tính hướng đẩy UV từ tâm ra ngoài
            vec2 dir = normalize(diff);
            totalOffset += dir * wave * u_strengths[i] * fade * 0.05;
        }
    }

    vec4 scene = texture(texture0, uv + totalOffset);

    // HDR SANITIZE (Đợt G) — single choke point feeding the post chain.
    // (1) Force alpha OPAQUE. The float scene buffer's alpha is a garbage
    //     byproduct of additive-particle accumulation (BLEND_ADDITIVE sums
    //     src_a² unbounded; LDR RGBA8 used to clamp it to 1). This pass blits
    //     into a BLACK-cleared mainRenderTex under BLEND_ALPHA, so
    //     main.rgb = scene.rgb * scene.a — a huge accumulated alpha blew rgb up
    //     to +Inf, then composite ACES(Inf)=NaN and the region rendered BLACK
    //     wherever many particles overlapped. Opaque alpha = clean rgb copy.
    // (2) NaN guard + finite cap on rgb: cheap insurance against half-float
    //     overflow (anything above ~4 already tone-maps to white anyway).
    // min() caps Inf/overflow (min(Inf,64)=64) and is GLES2-safe. NaN only
    // arises in the HDR float path (desktop/GLES3), and is now prevented at the
    // source (opaque shaders output alpha=1 — see ground_splat/path_blend), so a
    // bvec-mix NaN guard here isn't needed — and mix(...,bvec) is GLSL ES 3.00
    // only, which would fail to compile on GLES2 (#version 100).
    vec3 hdr = min(scene.rgb, vec3(64.0));
    finalColor = vec4(hdr * colDiffuse.rgb, 1.0);
}
