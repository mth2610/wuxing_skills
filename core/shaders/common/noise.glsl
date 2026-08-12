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

// 2D / 3D hash → [0, 1]
//
// MOBILE-SAFE (Dave Hoskins "Hash without Sine", https://www.shadertoy.com/view/4djSRW).
// The old `fract(sin(dot(p, K)) * 43758.5)` hash silently DIES on Mali/Adreno when the
// argument to sin() grows large: sin() loses precision for big inputs, the hash returns a
// near-constant, and any fbm built on it collapses to a flat value. Desktop (good sin
// precision) hid it. It broke exactly the effects that push the noise domain far — fbm3 with
// several octaves (black_hole_swirl.fs: invisible shells) and time-in-the-domain scroll
// (aura_shell.fs: `dom.y -= u_time*speed` grows unbounded → membrane with no swirl). The
// `fract(p * 0.1031)` step below bounds the working set to [0,1) BEFORE any arithmetic, so the
// result is magnitude-independent and identical on every GPU. Pattern differs slightly from the
// old hash (it's a different RNG) but it's still white noise — no VFX depends on the exact seed.
float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Dùng cho dissolve noise theo world-space: hash3(floor(fragPosition * scale))
float hash3(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
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

// 3D value noise → [0, 1]. The 3D companion to vnoise(), built on the same
// mobile-safe hash3.
//
// Reach for this instead of `sin(dot(worldPosition, k))` whenever a shader wants
// "some irregular variation over a 3D body". A single sine of a dot product is a
// PLANE WAVE: it paints parallel bands across whatever it touches, and on a
// curved body those bands read as interference fringes. That exact mistake has
// been made three times in core/fluid/shaders/fluid_surface.fs alone (a caustic
// lattice, a wave perturbation with a constant up-bias, and a "surfaceNoise"
// term driving roughness, glints and foam).
float vnoise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash3(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash3(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash3(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash3(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash3(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash3(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash3(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// 2-octave 3D value noise → [0, 1]. The second octave is offset as well as
// scaled, so the value-noise lattice of the first does not line up with it and
// the field has no axis-aligned grain.
float fbm3(vec3 p) {
    return (vnoise3(p) * 0.667 + vnoise3(p * 2.13 + vec3(17.3, 5.7, 11.1)) * 0.333);
}
