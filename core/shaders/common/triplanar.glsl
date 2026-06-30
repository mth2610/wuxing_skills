// ============================================================
// WUXING — Triplanar Mapping
// Include sau noise.glsl (cần vnoise) trong .fs của skill.
//
// Texture/màu procedural theo world-space cho mesh không có UV ổn định
// (ProceduralMesh Rock/ShardCluster/Fissure vẽ qua rlBegin immediate-mode,
// chỉ có position + normal, không có texcoord) — chiếu 3 mặt phẳng trục
// X/Y/Z lên fragPosition rồi blend theo |fragNormal|, tránh
// stretching/streaking trên facet có hướng UV biến dạng/không tồn tại.
//
// Cung cấp:
//   triplanarWeights(normal, sharpness) — trọng số blend 3 trục
//   triplanarNoise(pos, weights, scale) — pattern procedural (không cần texture)
//   triplanarSample(tex, pos, weights, scale) — sample texture thật
// ============================================================

// Trọng số blend 3 trục từ world normal. sharpness cao hơn -> chuyển tiếp
// giữa các mặt sắc nét hơn (gần 1 trục chiếm trọn), thấp hơn -> mượt hơn,
// blend rộng hơn ở các góc xiên. Thường dùng 2.0-6.0.
vec3 triplanarWeights(vec3 worldNormal, float sharpness) {
    vec3 w = pow(abs(worldNormal), vec3(sharpness));
    return w / max(w.x + w.y + w.z, 1e-5);
}

// Blend pattern procedural (vnoise) chiếu theo 3 trục — dùng khi material
// chưa có texture asset thật (vd. core_test demo). scale là tần số chiếu
// world-space, thường 0.02-0.1 tuỳ kích thước mesh.
float triplanarNoise(vec3 worldPos, vec3 weights, float scale) {
    float nx = vnoise(worldPos.yz * scale);
    float ny = vnoise(worldPos.xz * scale);
    float nz = vnoise(worldPos.xy * scale);
    return nx * weights.x + ny * weights.y + nz * weights.z;
}

// Blend 1 sampler2D chiếu theo 3 trục world-space — bản chính thức dùng cho
// material Earth/Metal thật khi có texture asset (đá/kim loại tileable).
// scale thường 0.01-0.05 tuỳ kích thước mesh.
vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 weights, float scale) {
    vec4 cx = texture(tex, worldPos.yz * scale);
    vec4 cy = texture(tex, worldPos.xz * scale);
    vec4 cz = texture(tex, worldPos.xy * scale);
    return cx * weights.x + cy * weights.y + cz * weights.z;
}
