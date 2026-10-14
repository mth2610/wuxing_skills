#version 330

in vec3 v_shadowData; // x: along [0..1], y: across [-1..1], z: precomputedStrength

uniform vec3 u_shadowTint;
out vec4 finalColor;

void main()
{
    float along = v_shadowData.x;
    float across = v_shadowData.y;

    // VISUAL UPGRADE: Bo tròn đầu bóng theo phương trình Elip tự nhiên thay vì cắt vuông góc
    // Khi tiến về ngọn bóng (along -> 1.0), khoảng cách ngang hiệu dụng tăng lên giúp bo tròn đỉnh
    float tipCurve = smoothstep(0.65, 1.0, along);
    float radialDist = sqrt(across * across + tipCurve * tipCurve);

    // VISUAL UPGRADE: Contact Hardening (Bóng sắc nét ở gốc cây, mờ rộng dần về phía ngọn)
    // Gốc cây (along = 0): bắt đầu mờ từ 0.55 -> 1.0
    // Ngọn bóng (along = 1): mờ mềm toàn bộ từ 0.15 -> 1.0
    float innerSoftness = mix(0.55, 0.15, along);
    float coverage = 1.0 - smoothstep(innerSoftness, 1.0, radialDist);

    // Khử hoàn toàn phần thừa ở mép cực đại của đỉnh bóng
    coverage *= 1.0 - smoothstep(0.85, 1.0, along);

    float finalAlpha = v_shadowData.z * coverage;

    // TỐI ƯU OVERDRAW: Bỏ qua việc ghi xuống Framebuffer nếu pixel ở rìa gần như trong suốt
    if (finalAlpha < 0.004) discard;

    vec3 targetTint = max(u_shadowTint, vec3(0.16));
    vec3 multiplier = mix(vec3(1.0), targetTint, finalAlpha);

    finalColor = vec4(multiplier, 1.0);
}