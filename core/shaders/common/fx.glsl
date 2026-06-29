// ============================================================
// WUXING — VFX Effect Utilities (Fragment Shader)
// Include trong .fs của skill khi cần dissolve, flow blend,
// emissive mask.
//
// KHÔNG phụ thuộc noise.glsl — dissolveCalc nhận noise value từ
// bên ngoài để tránh double-definition khi include cả hai.
//
// Cung cấp:
//   dissolveCalc()  — noise-based dissolve + edge glow
//   flowBlend()     — flow map 2-phase texture blend
//   emissiveMask()  — world-space sine emissive pattern
//
// Xem CORE_API.md §10 — Common Shader Files để biết cách dùng.
// ============================================================

// ------------------------------------------------------------------
// DISSOLVE  (noise-based pixel discard + edge glow)
//
// noiseVal  : giá trị ngẫu nhiên per-fragment [0..1].
//             Thường là hash3(floor(fragPosition * blockScale))
//             từ noise.glsl — skill tự tính trước khi gọi hàm này.
// dissolve  : tiến trình tan biến [0..1], thường là uniform u_dissolve
// edgeWidth : độ rộng viền cháy/sáng (0.05 – 0.12 thường tốt)
// edgeFactor (out): [0..1] — mức độ viền, dùng để mix màu edge glow
//
// Trả về: 0.0 = pixel còn nguyên, 1.0 = pixel bị xóa.
// Caller phải gọi discard khi kết quả >= 1.0.
//
// Ví dụ:
//   (include noise.glsl trước fx.glsl trong .fs của skill)
//   float n = hash3(floor(fragPosition * 10.0));
//   float edgeFactor;
//   if (dissolveCalc(n, u_dissolve, 0.08, edgeFactor) >= 1.0) discard;
//   vec3 edgeColor = ELEMENT_COLOR_FIRE * 2.0;   // element-specific
//   baseColor = mix(baseColor, edgeColor, edgeFactor);
// ------------------------------------------------------------------
float dissolveCalc(float noiseVal, float dissolve, float edgeWidth, out float edgeFactor) {
    edgeFactor = 0.0;
    if (noiseVal < dissolve) return 1.0;
    if (noiseVal < dissolve + edgeWidth)
        edgeFactor = 1.0 - smoothstep(dissolve, dissolve + edgeWidth, noiseVal);
    return 0.0;
}

// ------------------------------------------------------------------
// FLOW MAP 2-PHASE BLEND
//
// Lấy mẫu texture với kỹ thuật 2-pha chống giật (không có seam khi
// phase reset về 0). Dùng cho caustics, xoáy nước, lửa chảy...
//
// tex      : texture cần sample (caustics, vân sóng, lửa...)
// uv       : UV đã scale/tiling của texture chính
// flowDir  : hướng dòng chảy — lấy từ: texture(flowTex, fragTexCoord).rg * 2.0 - 1.0
// speed    : tốc độ cuộn (thường 0.5 – 2.0)
// strength : biên độ UV distortion (thường 0.03 – 0.15)
// time     : u_time
//
// Trả về channel r của texture sau khi blend — dùng trực tiếp như
// luminance cho caustics, hoặc nhân thêm màu element.
//
// Ví dụ:
//   vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
//   float intensity = flowBlend(causticsTex, fragTexCoord * 2.0, flowDir, 1.2, 0.05, u_time);
//   baseColor += waterCausticColor * intensity * 1.5;
// ------------------------------------------------------------------
float flowBlend(sampler2D tex, vec2 uv, vec2 flowDir, float speed, float strength, float time) {
    float p0 = fract(time * speed);
    float p1 = fract(time * speed + 0.5);
    float bw = abs(p0 * 2.0 - 1.0);
    float s0 = texture(tex, uv + flowDir * p0 * strength).r;
    float s1 = texture(tex, uv + flowDir * p1 * strength).r;
    return mix(s0, s1, bw);
}

// ------------------------------------------------------------------
// EMISSIVE MASK (world-space sine pattern)
//
// Tạo mặt nạ phát sáng lấp lánh từ vị trí world space. Không dùng
// UV — nghĩa là không bị kéo giãn/méo theo UV mapping của mesh.
// Dùng cho: nhựa cây chảy dọc thân, đường năng lượng trên lưỡi kiếm,
//           rạn nứt kim loại sáng, mạch nước ngầm trên đá...
//
// worldPos  : fragPosition
// freq      : tần số sóng (thường 0.5 – 3.0). Cao hơn = sọc dày hơn.
// threshold : ngưỡng bắt đầu phát sáng (thường 0.80 – 0.95).
//             Cao hơn = ít sáng hơn, tập trung hơn.
//
// Trả về [0..1]: 0 = không sáng, 1 = sáng hoàn toàn.
//
// Ví dụ:
//   float mask = emissiveMask(fragPosition, 1.5, 0.88);
//   baseColor += ELEMENT_COLOR_WOOD_VEC3 * mask * 2.5;
// ------------------------------------------------------------------
float emissiveMask(vec3 worldPos, float freq, float threshold) {
    float v = sin(worldPos.y * freq) * cos(worldPos.x * freq * 0.7);
    return smoothstep(threshold, 1.0, (v + 1.0) * 0.5);
}
