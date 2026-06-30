#version 330

#include "core/shaders/common/vs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/displacement.glsl"

uniform float u_radius;

void main() {
    vec3 localPos = vertexPosition;
    
    // Thu phóng bán kính cục bộ trước khi bẻ cong dọc Path
    localPos.x *= u_radius;
    localPos.z *= u_radius;

    // 1. Biến đổi uốn cong nằm rạp theo đường bay
    vec3 displaced = DisplaceVertex_AlongPath(localPos, vertexTexCoord);

    // 2. Tạo sóng gồ ghề nhấp nhô liên tục (Biên độ mạnh dần về phía đầu ngọn sóng)
    float waveDensity = fbm2(displaced.xz * 0.08 - vec2(0.0, u_time * 4.0));
    float crestIntensity = vertexTexCoord.y; // 0 ở đuôi, 1 ở đầu ngọn sóng
    
    displaced = DisplaceVertex_Noise(displaced, vertexNormal, waveDensity * crestIntensity * 8.0);

    // 3. Đẩy dữ liệu ra màn hình
    VS_FinalOutput(displaced);

    // Bắt buộc tính lại pháp tuyến cho ánh sáng chiếu đúng
    fragNormal = normalize(vec3(matModel * vec4(DisplaceVertex_AlongPathNormal(vertexNormal, vertexTexCoord), 0.0)));
}