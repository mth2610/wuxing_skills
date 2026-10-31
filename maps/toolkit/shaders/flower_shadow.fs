#version 330

in vec3 v_shadowData; // x: along [0..1], y: across [-1..1], z: precomputedStrength

uniform vec3 u_shadowTint;
out vec4 finalColor;

void main()
{
    float along = clamp(v_shadowData.x, 0.0, 1.0);
    float across = v_shadowData.y;

    // [VISUAL UPGRADE - BLOSSOM CROWN & STEM SHADOW PROFILE]:
    // Tạo hình bóng đổ thực tế của một khóm hoa:
    // - Gốc hoa (along < 0.25): Bóng tiếp xúc chân khóm hoa (Root Contact AO)
    // - Thân cuống (along 0.25..0.55): Thắt gọn theo dáng cuống và lá non
    // - Đài hoa (along 0.58..0.92): Phình tròn tạo bóng của tán cánh hoa trên mặt cỏ!
    float rootLobe  = exp(-along * 5.0) * 0.55;
    float stemWaist = 0.48;
    float bloomDiff = (along - 0.74) * 3.85;
    float bloomCrown = exp(-bloomDiff * bloomDiff) * 0.54;
    float halfWidth = clamp(stemWaist + rootLobe + bloomCrown, 0.42, 0.98);

    float normAcross = abs(across) / halfWidth;
    float tipEnd = smoothstep(0.78, 1.0, along);
    float radialDistSq = normAcross * normAcross + tipEnd * tipEnd;

    // Early-out nhanh trước khi gọi sqrt nếu điểm nằm ngoài bóng hoa
    if (radialDistSq >= 1.0) discard;

    float radialDist = sqrt(radialDistSq);

    // Contact Hardening: sắc nét ở gốc hoa và tâm đài hoa, tán xạ mềm ở rìa ngoài
    float innerSoftness = mix(0.65, 0.28, along);
    float coverage = 1.0 - smoothstep(innerSoftness, 1.0, radialDist);

    // Tăng nhẹ độ đậm bóng tại vùng gốc hoa và vùng tâm đài hoa
    float densityProfile = mix(1.12, 0.85, along) + bloomCrown * 0.35;
    float finalAlpha = clamp(v_shadowData.z * coverage * densityProfile, 0.0, 0.88);

    if (finalAlpha < 0.005) discard;

    vec3 targetTint = max(u_shadowTint * 0.85, vec3(0.12));
    vec3 multiplier = mix(vec3(1.0), targetTint, finalAlpha);

    finalColor = vec4(multiplier, 1.0);
}