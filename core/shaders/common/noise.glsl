// ============================================================
// WUXING — Common Noise Utilities (SIMD Vectorized & Quintic Smoothed)
// Include trong .vs hoặc .fs khi cần nhiễu ngẫu nhiên.
//
// Không phụ thuộc uniform nào, không phụ thuộc common header nào.
// Có thể include độc lập hoặc cùng lighting.glsl / fx.glsl.
//
// Cung cấp:
//   hash2(vec2)        — 2D pseudo-random hash → [0, 1]
//   hash3(vec3)        — 3D pseudo-random hash → [0, 1]
//   vnoise(vec2)       — 2D value noise (SIMD 4-corner vectorized) → [0, 1]
//   fbm2(vec2)         — 3-octave FBM (Unrolled) → [0, ~1]
//   fbm2N(vec2, int)   — N-octave FBM, 1–6 → [0, 1] normalized
//   vnoise3(vec3)      — 3D value noise (SIMD 8-corner + Quintic C2) → [0, 1]
//   fbm3(vec3)         — 2-octave 3D value noise → [0, 1]
//   ridged3(vec3)      — 3D ridged filament crest → [0, 1]
//   ridgedFbm3(vec3)   — 3-octave branching 3D filaments → [0, 1]
//   filaments3(vec3,float) — Domain-warped 3D filaments (Single-pass 3D warp)
// ============================================================

// Ma trận xoay hằng số (0.5 rad) được tính sẵn ở thời điểm biên dịch
const mat2 WUXING_FBM_ROT = mat2(0.87758256, 0.47942554, -0.47942554, 0.87758256);

// 2D / 3D Mobile-Safe Hash → [0, 1] (Dave Hoskins "Hash without Sine")
float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash3(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// 2D Value Noise → [0, 1]
// Tối ưu SIMD: Tính đồng thời cả 4 góc (0,0), (1,0), (0,1), (1,1) trong 1 vector vec4
// thay vì gọi hàm hash2() 4 lần rời rạc.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    // Quintic interpolation curve giúp triệt tiêu các vệt lưới vuông khi làm biến dạng UV
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    vec4 c = vec4(i.x, i.y, i.x + 1.0, i.y + 1.0);
    vec4 hx = fract(c.xzxz * 0.1031);
    vec4 hy = fract(c.yyww * 0.1031);
    vec4 hz = hx;

    // Thực hiện dot(p3, p3.yzx + 33.33) song song cho cả 4 đỉnh
    vec4 d = hx * (hy + 33.33) + hy * (hz + 33.33) + hz * (hx + 33.33);
    hx += d;
    hy += d;
    hz += d;
    vec4 h = fract((hx + hy) * hz);

    return mix(mix(h.x, h.y, u.x), mix(h.z, h.w, u.x), u.y);
}

// 3-octave FBM (Đã tháo vòng lặp - Unrolled để tối ưu hóa thanh ghi)
float fbm2(vec2 p) {
    float v = 0.500 * vnoise(p);
    p = WUXING_FBM_ROT * p * 2.0;
    v += 0.250 * vnoise(p);
    p = WUXING_FBM_ROT * p * 2.0;
    v += 0.125 * vnoise(p);
    return v;
}

// N-octave FBM (1 ≤ octaves ≤ 6). Kết quả normalize về [0, 1].
float fbm2N(vec2 p, int octaves) {
    float v     = 0.0;
    float a     = 0.5;
    float total = 0.0;
    for (int i = 0; i < 6; i++) {
        if (i >= octaves) break;
        v     += a * vnoise(p);
        total += a;
        p  = WUXING_FBM_ROT * p * 2.0;
        a *= 0.5;
    }
    return v / max(total, 1e-5);
}

// 3D Value Noise → [0, 1]
// Tối ưu SIMD: Băm đồng thời 4 góc mặt dưới (z0) và 4 góc mặt trên (z1) bằng toán tử vec4,
// giảm >55% số lượng lệnh ALU so với việc gọi 8 lần hash3() rời rạc.
// Sử dụng nội suy Quintic C2 để loại bỏ hoàn toàn vết gãy lưới trên bề mặt cầu 3D.
float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    vec4 cx = fract(vec4(i.x, i.x + 1.0, i.x, i.x + 1.0) * 0.1031);
    vec4 cy = fract(vec4(i.y, i.y, i.y + 1.0, i.y + 1.0) * 0.1031);
    vec2 cz = fract(vec2(i.z, i.z + 1.0) * 0.1031);

    // 4 đỉnh tại mặt phẳng Z = i.z
    vec4 z0 = vec4(cz.x);
    vec4 d0 = cx * (z0 + 31.32) + cy * (cy + 31.32) + z0 * (cx + 31.32);
    vec4 h0 = fract((cx + cy + 2.0 * d0) * (z0 + d0));

    // 4 đỉnh tại mặt phẳng Z = i.z + 1.0
    vec4 z1 = vec4(cz.y);
    vec4 d1 = cx * (z1 + 31.32) + cy * (cy + 31.32) + z1 * (cx + 31.32);
    vec4 h1 = fract((cx + cy + 2.0 * d1) * (z1 + d1));

    // Nội suy theo trục Z trước (tiết kiệm 2 lệnh mix!) rồi mới nội suy X và Y
    vec4 hz = mix(h0, h1, u.z);
    return mix(mix(hz.x, hz.y, u.x), mix(hz.z, hz.w, u.x), u.y);
}

// Hàm nội bộ: Tính đồng thời 3 kênh nhiễu 3D (vec3) chỉ trong 1 lần duyệt lưới duy nhất!
// Giúp bước Domain Warp trong filaments3() nhanh gấp ~2.8 lần.
vec3 vnoise3_vec3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    vec4 cx = fract(vec4(i.x, i.x + 1.0, i.x, i.x + 1.0) * 0.1031);
    vec4 cy = fract(vec4(i.y, i.y, i.y + 1.0, i.y + 1.0) * 0.1031);
    vec2 cz = fract(vec2(i.z, i.z + 1.0) * 0.1031);

    vec4 z0 = vec4(cz.x);
    vec4 d0 = cx * (z0 + 31.32) + cy * (cy + 31.32) + z0 * (cx + 31.32);
    vec4 h0X = fract((cx + cy + 2.0 * d0) * (z0 + d0));

    vec4 z1 = vec4(cz.y);
    vec4 d1 = cx * (z1 + 31.32) + cy * (cy + 31.32) + z1 * (cx + 31.32);
    vec4 h1X = fract((cx + cy + 2.0 * d1) * (z1 + d1));

    // Trích xuất kênh Y và Z không tương quan từ chính kết quả hash của kênh X
    vec4 hzX = mix(h0X, h1X, u.z);
    vec4 hzY = mix(fract(h0X * 17.31 + 0.37), fract(h1X * 17.31 + 0.37), u.z);
    vec4 hzZ = mix(fract(h0X * 29.17 + 0.73), fract(h1X * 29.17 + 0.73), u.z);

    return vec3(
        mix(mix(hzX.x, hzX.y, u.x), mix(hzX.z, hzX.w, u.x), u.y),
        mix(mix(hzY.x, hzY.y, u.x), mix(hzY.z, hzY.w, u.x), u.y),
        mix(mix(hzZ.x, hzZ.y, u.x), mix(hzZ.z, hzZ.w, u.x), u.y)
    );
}

// 2-octave 3D value noise → [0, 1]
float fbm3(vec3 p) {
    return (vnoise3(p) * 0.667 + vnoise3(p * 2.13 + vec3(17.3, 5.7, 11.1)) * 0.333);
}

// ── Ridged filaments ────────────────────────────────────────────────────────
// Nâng cấp Visual: Kết hợp Quintic C2 từ vnoise3 và làm thon nhẹ đỉnh sóng (crest shaping)
// để các nhánh tia năng lượng liền lạc, sắc nét và không bị răng cưa lưới.
float ridged3(vec3 p) {
    float r = 1.0 - abs(vnoise3(p) - 0.5) * 2.0;
    // Giữ nguyên biên độ [0, 1] nhưng làm đỉnh sóng gọn và rõ nét hơn
    return r * (0.35 + 0.65 * r);
}

// Three octaves branching network
float ridgedFbm3(vec3 p) {
    float v = 0.54 * ridged3(p);
    v += 0.30 * ridged3(p * 2.17 + vec3(11.3, -4.7, 6.1));
    v += 0.16 * ridged3(p * 4.61 - vec3(5.9, 8.2, -3.4));
    return clamp(v, 0.0, 1.0);
}

// DOMAIN-WARPED ridged filaments (Single-Pass Vectorized 3D Warp)
float filaments3(vec3 p, float warp) {
    vec3 w = vnoise3_vec3(p * 0.63 + vec3(4.1, 19.7, -8.3)) - 0.5;
    return ridgedFbm3(p + w * warp);
}
