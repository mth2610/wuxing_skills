// ============================================================
// WUXING — Triplanar Mapping (ALU, Bandwidth & Visually Optimized)
// Include sau noise.glsl (cần vnoise) trong .fs của skill.
//
// Texture/màu procedural theo world-space cho mesh không có UV ổn định
// (ProceduralMesh Rock/ShardCluster/Fissure vẽ qua rlBegin immediate-mode,
// chỉ có position + normal, không có texcoord) — chiếu 3 mặt phẳng trục
// X/Y/Z lên fragPosition rồi blend theo |fragNormal|, tránh
// stretching/streaking trên facet có hướng UV biến dạng/không tồn tại.
//
// Cung cấp:
//   triplanarWeights(normal, sharpness)       — trọng số blend 3 trục (Không dùng pow)
//   triplanarNoise(pos, weights, scale)       — pattern procedural (Bảo toàn tương phản + Culling)
//   triplanarSample(tex, pos, weights, scale) — sample texture thật (Zero-Mirror + Culling)
// ============================================================

// Trọng số blend 3 trục từ world normal.
// Tối ưu: Thay thế pow(vec3) đắt đỏ bằng kỹ thuật Tightened Box-Blend + Rational Power.
// Tự động triệt tiêu các trục có đóng góp quá nhỏ (< 1%) về đúng 0.0 để kích hoạt
// Early-Cull khi lấy mẫu Texture/Noise ở các hàm bên dưới!
vec3 triplanarWeights(vec3 worldNormal, float sharpness) {
    vec3 w = abs(worldNormal);

    // Thu hẹp vùng giao thoa thừa (0.2679 ~ tan(15 deg)) để giữ độ sắc nét của vân đá
    // và giúp hầu hết các mặt phẳng nghiêng chỉ cần lấy mẫu 1 hoặc 2 trục thay vì cả 3.
    float tighten = clamp((sharpness - 1.0) * 0.06, 0.0, 0.25);
    w = max(w - tighten, 0.0);

    // Xấp xỉ lũy thừa không dùng SFU pow(): kết hợp bình phương lặp theo sharpness
    vec3 w2 = w * w;
    vec3 w4 = w2 * w2;
    w = (sharpness > 3.0) ? mix(w2, w4, clamp((sharpness - 2.0) * 0.25, 0.0, 1.0))
                          : mix(w,  w2, clamp(sharpness - 1.0, 0.0, 1.0));

    return w / max(w.x + w.y + w.z, 1e-5);
}

// Blend pattern procedural (vnoise) chiếu theo 3 trục.
// Tối ưu Hiệu năng: Bỏ qua hoàn toàn việc tính vnoise() cho các trục có weight <= 0.005.
// Nâng cấp Visual:
//   1. Dịch pha tọa độ giữa 3 trục (Phase Offsets) để xóa bỏ vết đối xứng gương ở góc vát 45 độ.
//   2. Bù hệ số phương sai (Variance-Preserving) để góc đá không bị mờ đục khi trộn.
float triplanarNoise(vec3 worldPos, vec3 weights, float scale) {
    vec3 p = worldPos * scale;
    float n = 0.0;

    if (weights.x > 0.005) n += vnoise(p.yz + vec2(17.3, 31.7)) * weights.x;
    if (weights.y > 0.005) n += vnoise(p.xz + vec2(43.1, 11.9)) * weights.y;
    if (weights.z > 0.005) n += vnoise(p.xy + vec2(29.5, 53.3)) * weights.z;

    // Bảo toàn độ tương phản (Variance Preservation) tại vùng giao thoa giữa các mặt
    float weightLen = sqrt(dot(weights, weights));
    return clamp(0.5 + (n - 0.5) / max(mix(1.0, weightLen, 0.65), 0.5), 0.0, 1.0);
}

// Blend 1 sampler2D chiếu theo 3 trục world-space.
// Tối ưu Hiệu năng: Dùng đạo hàm giải tích (textureGrad) kết hợp nhánh lọc (Weight Culling)
// giúp tiết kiệm 33% - 66% số lần đọc Texture trên các mặt phẳng/vách đá nghiêng!
// Nâng cấp Visual: Dịch pha bất đối xứng nhẹ để phá hiện tượng lặp hoa văn ở rìa.
vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 weights, float scale) {
    vec3 p = worldPos * scale;
    // Tính sẵn đạo hàm 1 lần cho worldPos giúp GPU chọn đúng Mipmap mà không bị lỗi
    // seam mipmap khi rẽ nhánh lấy mẫu theo từng trục.
    vec3 dpdx = dFdx(p);
    vec3 dpdy = dFdy(p);

    vec4 col = vec4(0.0);
    if (weights.x > 0.005) {
        col += textureGrad(tex, p.yz + vec2(0.37, 0.71), dpdx.yz, dpdy.yz) * weights.x;
    }
    if (weights.y > 0.005) {
        col += textureGrad(tex, p.xz, dpdx.xz, dpdy.xz) * weights.y;
    }
    if (weights.z > 0.005) {
        col += textureGrad(tex, p.xy + vec2(0.61, 0.23), dpdx.xy, dpdy.xy) * weights.z;
    }
    return col;
}