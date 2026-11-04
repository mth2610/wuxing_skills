#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

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

    int safeCount = clamp(u_count, 0, 8);
    float aspect = max(u_aspectRatio, 0.0001);
    float invAspect = 1.0 / aspect;

    for (int i = 0; i < safeCount; i++) {
        float rad = u_radii[i];
        float prog = clamp(u_progress[i], 0.0, 1.0);
        float currentRadius = rad * prog;
        if (currentRadius <= 0.0001) continue;

        vec2 diff = uv - u_centers[i];
        diff.y *= invAspect;

        // So sánh bình phương khoảng cách trước để 95% pixel ngoài vòng sóng không phải tính sqrt()
        float distSq = dot(diff, diff);
        float innerEdge = max(currentRadius - (rad * 0.15), 0.0);

        if (distSq < currentRadius * currentRadius && distSq > innerEdge * innerEdge) {
            float dist = sqrt(distSq);
            float invHalfRing = 13.3333333 / max(rad, 0.0001);
            float x = clamp((dist - currentRadius) * invHalfRing + 1.0, -1.0, 1.0);

            float strengthFactor = u_strengths[i] * (1.0 - prog) * 0.05;
            float wave = sin(x * 3.14159265);
            // Hàm envelope bậc hai (1 - x^2) đạo hàm liên tục C1, xóa vết gãy khúc xạ ở tâm và rìa sóng
            float fade = 1.0 - x * x;

            vec2 dir = diff / max(dist, 0.00001);
            // Quy đổi độ lệch trục Y về lại không gian UV của màn hình
            dir.y *= aspect;

            totalOffset += dir * (wave * fade * strengthFactor);
        }
    }

    vec2 sampleUv = clamp(uv + totalOffset, vec2(0.001), vec2(0.999));
    vec4 scene = texture(texture0, sampleUv);
    vec3 hdr = clamp(scene.rgb, vec3(0.0), vec3(64.0));
    finalColor = vec4(hdr * colDiffuse.rgb, 1.0);
}