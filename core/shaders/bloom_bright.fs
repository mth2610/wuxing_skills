#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_threshold;
// Bloom is authored at quarter resolution, but the scene source is full size.
// A single sample here would skip a one-pixel HDR mesh core completely. This
// footprint gathers the full 4x4 source-pixel cell before thresholding.
uniform vec2 u_sourceTexelSize;
// Per-pixel cap on how much energy one pixel may contribute to the bloom.
// This is the REAL ceiling on "make the core glow harder": past it, raising a
// particle's emissiveBoost changes nothing at all, because every pixel of the
// core is already clamped here. It exists to stop fireflies, so it is a
// uniform (tuning.cfg -> bloom_max_energy) rather than a constant — 4.0
// reproduces the old behaviour exactly.
uniform float u_maxEnergy;

out vec4 finalColor;

void main() {
    // Preserve sub-pixel / thin HDR features (lightning cores, rune lines,
    // mesh glints) while keeping bloom itself at quarter resolution. The
    // brightest source sample owns the prefilter; the energy cap below remains
    // the firefly guard, so this cannot inject unbounded light into the blur.
    vec3 col = vec3(0.0);
    float bestBrightness = -1.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 cellOffset = (vec2(float(x), float(y)) - vec2(1.5)) * u_sourceTexelSize;
            vec3 sampleColor = texture(texture0, fragTexCoord + cellOffset).rgb;
            float sampleBrightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
            if (sampleBrightness > bestBrightness) {
                bestBrightness = sampleBrightness;
                col = sampleColor;
            }
        }
    }
    
    // 1. Tính toán độ sáng dựa trên kênh màu lớn nhất (max-channel) để các màu bão hòa (lam, tím) cũng bloom rực rỡ
    float brightness = max(col.r, max(col.g, col.b));
    
    // 2. Thuật toán chống ảo ảnh cho vật thể mảnh (Knee Anti-Aliasing)
    // Tạo một vùng đệm mượt mờ quanh ngưỡng threshold để triệt tiêu răng cưa
    float knee = 0.15; 
    float soft = brightness - u_threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    
    // Cường độ ánh sáng được làm mượt bằng hàm bậc hai
    float weight = max(soft, brightness - u_threshold);
    weight /= max(brightness, 0.00001);
    
    // 3. Chặn triệt để hiện tượng "Lóa điểm" (Fireflies) khi áp sát camera
    // Giới hạn không cho năng lượng pixel tăng đột biến quá mức kiểm soát của bộ Blur
    // Boost bloom extraction weight slightly to offset clamping dilution
    vec3 brightColor = col.rgb * weight * 2.2;
    float maxEnergy = (u_maxEnergy > 0.0) ? u_maxEnergy : 4.0;
    float currentEnergy = length(brightColor);
    if (currentEnergy > maxEnergy) {
        brightColor = (brightColor / currentEnergy) * maxEnergy;
    }
    
    finalColor = vec4(brightColor, 1.0);
}
