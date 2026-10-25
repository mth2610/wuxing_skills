#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_threshold;
uniform float u_exposure;
uniform vec2 u_sourceTexelSize;
uniform float u_maxEnergy;
uniform float u_knee;

out vec4 finalColor;

void main() {
    float expVal = max(u_exposure, 0.0001);
    // Quy đổi ngưỡng về không gian trước phơi sáng để tránh nhân expVal 16 lần trong vòng lặp
    float rawThreshold = u_threshold / expVal;

    vec3 col = vec3(0.0);
    float bestRawBrightness = -1.0;
    float brightCount = 0.0;

    // 9-tap (3x3) bilinear-assisted brightest-sample prefilter across the 4x4 source cell:
    // preserves 1-pixel hot HDR emitters (Landmine #6) while cutting texture fetches by 44% (16 -> 9).
    for (int y = -1; y <= 1; ++y) {
        float offsetY = float(y) * 1.15 * u_sourceTexelSize.y;
        for (int x = -1; x <= 1; ++x) {
            vec2 cellOffset = vec2(float(x) * 1.15 * u_sourceTexelSize.x, offsetY);
            vec3 sampleColor = texture(texture0, fragTexCoord + cellOffset).rgb;
            float rawBrightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
            if (rawBrightness > bestRawBrightness) {
                bestRawBrightness = rawBrightness;
                col = sampleColor;
            }
            brightCount += step(rawThreshold, rawBrightness);
        }
    }

    // 1. Độ sáng lớn nhất đã tìm được từ vòng lặp, không cần tính lại max(col.r, ...)
    float brightness = bestRawBrightness * expVal;

    // 2. Kiểm tra ngưỡng Knee và thoát sớm TRƯỚC khi gọi hàm căn bậc hai sqrt()
    float knee = (u_knee > 0.0) ? u_knee : 0.15;
    float soft = brightness - u_threshold + knee;
    if (soft <= 0.0) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Chỉ những pixel thực sự vượt ngưỡng mới tính sqrt() (chuẩn hóa cho 9 mẫu: 1/9 = 0.111111)
    float coverage = sqrt(max(brightCount * 0.111111, 0.0625));

    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);

    float weight = max(soft, brightness - u_threshold) / max(brightness, 0.00001);

    // 3. Soft ceiling (Reinhard-shaped asymptote)
    vec3 brightColor = col * (weight * 2.2 * coverage);
    float maxEnergy = (u_maxEnergy > 0.0) ? u_maxEnergy : 12.0;
    float energy = max(brightColor.r, max(brightColor.g, brightColor.b));
    brightColor *= maxEnergy / (maxEnergy + energy);

    finalColor = vec4(brightColor, 1.0);
}