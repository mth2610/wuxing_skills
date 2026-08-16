#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_threshold;
// Exposure is applied only for threshold/weight decisions. The written bloom
// colour stays in raw scene-linear HDR so post_process.fs applies exposure once
// to scene+bloom together (no double exposure in the pyramid).
uniform float u_exposure;
// Bloom is authored at quarter resolution, but the scene source is full size.
// A single sample here would skip a one-pixel HDR mesh core completely. This
// footprint gathers the full 4x4 source-pixel cell before thresholding.
uniform vec2 u_sourceTexelSize;
// SOFT ceiling on how much energy one pixel may contribute to the bloom.
//
// This used to be a HARD clamp at 4.0: past it, raising a particle's
// emissiveBoost changed nothing at all, because every pixel of the core was
// already cut to the same length. That was the real trần on "make the core glow
// harder". It is now an asymptote instead — strictly increasing, so more
// emissive always reads as more bloom, with diminishing returns rather than a
// wall. Isolated fireflies are handled where they belong, by the Karis-weighted
// first downsample (bloom_downsample.fs, u_karis), which can tell a one-texel
// spike from a genuinely bright core; this shader cannot.
// tuning.cfg -> bloom_max_energy. <= 0 falls back to the default below.
uniform float u_maxEnergy;
// Width of the smooth ramp below the threshold. Wider = bloom fades in over a
// range of brightness instead of switching on, which is most of what stops a
// moving highlight from crawling. tuning.cfg -> bloom_knee.
uniform float u_knee;

out vec4 finalColor;

void main() {
    // Preserve sub-pixel / thin HDR features (lightning cores, rune lines,
    // mesh glints) while keeping bloom itself at quarter resolution. The
    // brightest source sample owns the prefilter; the Karis-weighted downsample
    // that follows is the firefly guard, so this cannot inject an unbounded
    // spike into the blur.
    vec3 col = vec3(0.0);
    float bestBrightness = -1.0;
    float brightCount = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 cellOffset = (vec2(float(x), float(y)) - vec2(1.5)) * u_sourceTexelSize;
            vec3 sampleColor = texture(texture0, fragTexCoord + cellOffset).rgb;
            float sampleBrightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b)) * max(u_exposure, 0.0001);
            if (sampleBrightness > bestBrightness) {
                bestBrightness = sampleBrightness;
                col = sampleColor;
            }
            if (sampleBrightness > u_threshold) brightCount += 1.0;
        }
    }
    // How much of the 4x4 cell is actually lit. Taking the max above preserves a
    // one-pixel feature's PRESENCE, but on its own it also inflates that
    // feature's ENERGY by up to 16x, because a single hot texel is then treated
    // as though it filled the cell. While the old hard clamp existed that
    // inflation was capped and invisible; lifting the ceiling exposed it, and a
    // thin lightning filament bloomed into a fat white tube with its blue core
    // washed out.
    //
    // sqrt, not linear: linear coverage is energy-exact but reduces a one-pixel
    // core to 1/16, which is how thin emitters vanished from bloom in the first
    // place (core/docs/LANDMINES.md). sqrt(1/16) = 1/4 keeps them clearly
    // present while removing most of the over-contribution.
    //
    // A FULLY covered cell is multiplied by exactly 1.0 — but that is not the
    // same as "broad glows are unaffected", and the difference was measured,
    // not assumed: a large emissive body still loses roughly 10-15% of its
    // bloom, because its rim and its mid-brightness regions are partially
    // covered cells too. bloomIntensity is the knob that compensates; the point
    // of this factor is that thin and broad features now scale TOGETHER, so one
    // intensity setting can serve both.
    float coverage = sqrt(max(brightCount / 16.0, 1.0 / 16.0));

    // 1. Tính toán độ sáng dựa trên kênh màu lớn nhất (max-channel) để các màu bão hòa (lam, tím) cũng bloom rực rỡ
    float brightness = max(col.r, max(col.g, col.b)) * max(u_exposure, 0.0001);

    // 2. Thuật toán chống ảo ảnh cho vật thể mảnh (Knee Anti-Aliasing)
    // Tạo một vùng đệm mượt mờ quanh ngưỡng threshold để triệt tiêu răng cưa
    float knee = (u_knee > 0.0) ? u_knee : 0.15;
    float soft = brightness - u_threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);

    // Cường độ ánh sáng được làm mượt bằng hàm bậc hai
    float weight = max(soft, brightness - u_threshold);
    weight /= max(brightness, 0.00001);

    // 3. Soft ceiling. Reinhard-shaped: out = e * M / (M + e), which is ~identity
    // for e << M and approaches M asymptotically — never a flat cut, so the
    // derivative w.r.t. emissive brightness stays positive everywhere.
    // The 2.2 gain is kept from the clamped era ON PURPOSE: it is what makes
    // mid-brightness bloom come out at the same level as before this change, so
    // the only visible difference is that bright cores are no longer capped.
    // Drop it and every existing effect quietly dims by ~2x.
    vec3 brightColor = col.rgb * weight * 2.2 * coverage;
    float maxEnergy = (u_maxEnergy > 0.0) ? u_maxEnergy : 12.0;
    float energy = max(brightColor.r, max(brightColor.g, brightColor.b));
    brightColor *= maxEnergy / (maxEnergy + energy);

    finalColor = vec4(brightColor, 1.0);
}
