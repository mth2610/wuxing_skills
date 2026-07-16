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

    // FIX 1: Chặn đứng lỗi tràn mảng (Out-Of-Bounds) bằng điều kiện i < 8
    // Ngay cả khi u_count bị rác hoặc > 8, GPU cũng tuyệt đối không đọc lố bộ nhớ.
    int safeCount = min(u_count, 8); 

    for (int i = 0; i < safeCount; i++) {
        vec2 diff = uv - u_centers[i];
        diff.y /= u_aspectRatio;
        
        float dist = length(diff);
        float currentRadius = u_radii[i] * u_progress[i];
        float ringWidth = u_radii[i] * 0.15; 
        
        if (dist < currentRadius && dist > currentRadius - ringWidth) {
            float mid = currentRadius - ringWidth * 0.5;
            float x = (dist - mid) / (ringWidth * 0.5); 
            
            float wave = sin(x * 3.14159265);
            float fade = (1.0 - u_progress[i]) * (1.0 - abs(x));
            
            // FIX 2: Ngăn chặn lỗi chia cho 0 (NaN) khi pixel trùng khít với tâm
            if (dist > 0.0001) {
                vec2 dir = diff / dist; // Tối ưu: Dùng luôn dist đã tính thay vì gọi lại normalize()
                totalOffset += dir * wave * u_strengths[i] * fade * 0.05;
            }
        }
    }

    vec4 scene = texture(texture0, uv + totalOffset);
    vec3 hdr = min(scene.rgb, vec3(64.0));
    finalColor = vec4(hdr * colDiffuse.rgb, 1.0);
}