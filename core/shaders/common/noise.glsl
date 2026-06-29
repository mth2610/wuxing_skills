// ============================================================
// WUXING — Common Noise Utilities
// Include trong .vs hoặc .fs khi cần nhiễu ngẫu nhiên.
//
// Không phụ thuộc uniform nào, không phụ thuộc common header nào.
// Có thể include độc lập hoặc cùng lighting.glsl / fx.glsl.
//
// Cung cấp:
//   hash2(vec2)        — 2D pseudo-random hash → [0, 1]
//   hash3(vec3)        — 3D pseudo-random hash → [0, 1]
//   vnoise(vec2)       — 2D value noise        → [0, 1]
//   fbm2(vec2)         — 3-octave FBM          → [0, ~1]
//   fbm2N(vec2, int)   — N-octave FBM, 1–6     → [0, 1] normalized
//
// LƯU Ý ĐẶT TÊN:
//   Hàm value noise có tên "vnoise" (không phải "noise2") để tránh
//   conflict với GLSL built-in noise2(genType) → vec2.
//
// PHÁP TẮC:
//   Không tái implement hash/noise/fbm trong skill code — dùng file này.
//   fx.glsl không phụ thuộc noise.glsl — có thể include riêng lẻ.
// ============================================================

// 2D hash → [0, 1]
float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// 3D hash → [0, 1]
// Dùng cho dissolve noise theo world-space: hash3(floor(fragPosition * scale))
float hash3(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453123);
}

// 2D value noise → [0, 1]
// Tên "vnoise" để tránh conflict với GLSL built-in noise2().
// Nhanh hơn Perlin. Đủ cho FBM, warp UV, filament distortion.
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash2(i + vec2(0.0, 0.0)), hash2(i + vec2(1.0, 0.0)), u.x),
        mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// 3-octave FBM (Fractional Brownian Motion) — đủ cho hầu hết VFX.
// Output ≈ [0, 1], không normalize cứng.
// Rotation 0.5 rad giữa các octave để tránh axis-aligned artifacts.
float fbm2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 3; i++) {
        v += a * vnoise(p);
        p  = rot * p * 2.0;
        a *= 0.5;
    }
    return v;
}

// N-octave FBM (1 ≤ octaves ≤ 6). Kết quả normalize về [0, 1].
//   1-2 octave = mịn/nhanh   (gió nhẹ, hào quang mềm)
//   3-4 octave = chuẩn       (lửa, mây, nước)
//   5-6 octave = chi tiết    (vỏ cây, đá, da thịt)
float fbm2N(vec2 p, int octaves) {
    float v     = 0.0;
    float a     = 0.5;
    float total = 0.0;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 6; i++) {
        if (i >= octaves) break;
        v     += a * vnoise(p);
        total += a;
        p  = rot * p * 2.0;
        a *= 0.5;
    }
    return v / total;
}
