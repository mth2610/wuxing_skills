#version 330

// Input attributes from vertex shader
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniforms
uniform sampler2D texture0; // Cảnh nền đã render (RenderTexture)
uniform sampler2D u_vfxBodyTex;
uniform int u_hasVfxLayers;
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

    // FIX 1: Chặn đứng lỗi tràn mảng (Out-Of-Bounds)
    int safeCount = min(u_count, 8); 

    // [TỐI ƯU 1]: Tính trước nghịch đảo để thay thế phép chia (division) bằng phép nhân (multiplication).
    // GPU thực hiện phép nhân nhanh hơn phép chia rất nhiều.
    float invAspect = 1.0 / u_aspectRatio;

    for (int i = 0; i < safeCount; i++) {
        vec2 diff = uv - u_centers[i];
        diff.y *= invAspect; // Tối ưu 1 áp dụng
        
        float dist = length(diff);
        
        // Trích xuất biến mảng để compiler dễ dàng tối ưu cache
        float rad = u_radii[i];
        float prog = u_progress[i];
        
        float currentRadius = rad * prog;
        float innerEdge = currentRadius - (rad * 0.15);
        
        // VẪN GIỮ LỆNH IF NÀY: Mặc dù rẽ nhánh (branching) thường gây chậm shader, 
        // nhưng đây là "Spatial Branch". Nó giúp GPU bỏ qua tính toán cho 95% pixel 
        // không nằm trong diện tích của vòng sóng. Việc này có lợi hơn rất nhiều.
        if (dist < currentRadius && dist > innerEdge) {
            
            // [TỐI ƯU 2]: Khử phép chia cho biến động (Variable Division).
            // Thay vì tính halfRing = rad * 0.075 rồi lấy (dist - mid) / halfRing (rất tốn kém),
            // Ta có: 1 / 0.075 = 13.333333. Nghịch đảo của halfRing luôn là 13.333333 / rad.
            float invHalfRing = 13.3333333 / rad; 
            
            // [TỐI ƯU 3]: Rút gọn đại số công thức tìm x. Tiết kiệm 1 biến trung gian (mid) 
            // và giảm số lượng lệnh cộng trừ.
            float x = (dist - currentRadius) * invHalfRing + 1.0; 
            
            // Gom các hằng số không phụ thuộc vào tọa độ uv lại thành một cụm nhân trước.
            float strengthFactor = u_strengths[i] * (1.0 - prog) * 0.05;
            
            float wave = sin(x * 3.14159265);
            float fade = 1.0 - abs(x);
            
            // [TỐI ƯU 4]: Triệt tiêu nhánh lồng nhau (Nested Branch) `if (dist > 0.0001)`.
            // Rẽ nhánh lồng nhau gây lỗi Divergence trên các Warp/Wavefront của GPU.
            // Dùng hàm max() được hỗ trợ thẳng từ phần cứng (1 chu kỳ clock) để tránh chia cho 0.
            vec2 dir = diff / max(dist, 0.00001); 
            
            totalOffset += dir * (wave * fade * strengthFactor);
        }
    }

    vec2 sampleUv = uv + totalOffset;
    vec4 scene = texture(texture0, sampleUv);
    vec4 body = (u_hasVfxLayers != 0) ? texture(u_vfxBodyTex, sampleUv) : vec4(0.0);
    // Body buffer is stored with regular alpha blending, hence RGB is
    // premultiplied. Recover straight colour, then composite with the STORED
    // coverage. Coverage must stay linear here: a global nonlinear expansion
    // turns every producer's soft 0.1 edge into a visible ~0.47 silhouette and
    // defeats smoke/particle/decal edge masks after they have already run.
    float bodyCoverage = body.a;
    vec3 bodyColor = (body.a > 0.0001) ? (body.rgb / body.a) : vec3(0.0);
    scene.rgb = scene.rgb * (1.0 - bodyCoverage) + bodyColor * bodyCoverage;
    vec3 hdr = min(scene.rgb, vec3(64.0));
    finalColor = vec4(hdr * colDiffuse.rgb, 1.0);
}
