#version 330

in vec3 v_shadowData; // x: along [0..1], y: across [-1..1], z: precomputedStrength

uniform vec3 u_shadowTint;
out vec4 finalColor;

void main()
{
    float along = clamp(v_shadowData.x, 0.0, 1.0);
    float across = v_shadowData.y;

    // [VISUAL UPGRADE & ALU OPTIMIZATION - BLOSSOM CROWN & STEM SHADOW PROFILE]:
    // Dùng xấp xỉ hữu tỉ 1 / (1 + x + 0.48 * x^2) thay cho 2 lệnh exp()
    float rArg = along * 5.0;
    float rootLobe = 0.55 / (1.0 + rArg + 0.48 * rArg * rArg);

    float stemWaist = 0.48;
    float bloomDiff = (along - 0.74) * 3.85;
    float bArg = bloomDiff * bloomDiff;
    float bloomCrown = 0.54 / (1.0 + bArg + 0.48 * bArg * bArg);

    float halfWidth = clamp(stemWaist + rootLobe + bloomCrown, 0.42, 0.98);

    float normAcross = abs(across) / halfWidth;
    float tipEnd = smoothstep(0.78, 1.0, along);
    float radialDistSq = normAcross * normAcross + tipEnd * tipEnd;

    // Early-out nhanh trước khi tính căn bậc hai nếu điểm nằm ngoài bóng hoa
    if (radialDistSq >= 1.0) discard;

    // Contact Hardening trực tiếp trên miền bình phương khoảng cách (không cần gọi sqrt)
    float innerSoftness = mix(0.65, 0.28, along);
    float coverage = 1.0 - smoothstep(innerSoftness * innerSoftness, 1.0, radialDistSq);

    float densityProfile = mix(1.12, 0.85, along) + bloomCrown * 0.35;
    float finalAlpha = clamp(v_shadowData.z * coverage * densityProfile, 0.0, 0.88);

    if (finalAlpha < 0.005) discard;

    vec3 targetTint = max(u_shadowTint * 0.85, vec3(0.12));
    vec3 multiplier = mix(vec3(1.0), targetTint, finalAlpha);

    finalColor = vec4(multiplier, 1.0);
}