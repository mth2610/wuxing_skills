#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;   // world-space position
in vec3 fragNormal;     // world-space normal, hướng từ tâm cầu ra ngoài

uniform float u_time;
uniform vec3 viewPos;

// Texture trắng đen (đã convert từ Mac)
uniform sampler2D causticsTex;
uniform sampler2D flowTex;

// Đổi prefix sang uFlow* để khớp module flow_map dùng chung cho nhiều skill,
// tránh đụng tên với uniform riêng của từng shader (ví dụ fire.fs, fluid.fs).
uniform float uFlowSpeed = 1.2;
uniform float uFlowStrength = 0.05;
uniform float uFlowTiling = 1.0;

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord * uFlowTiling;

    // 1. KỸ THUẬT FLOW MAP 2 PHA - tạo dòng chảy xoáy liên tục, không giật
    vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
    float phase0 = fract(u_time * uFlowSpeed);
    float phase1 = fract(u_time * uFlowSpeed + 0.5);
    float blendWeight = abs(phase0 * 2.0 - 1.0);

    vec2 uv0 = uv + flowDir * phase0 * uFlowStrength;
    vec2 uv1 = uv + flowDir * phase1 * uFlowStrength;

    float tex0 = texture(causticsTex, uv0).r;
    float tex1 = texture(causticsTex, uv1).r;
    float intensity = mix(tex0, tex1, blendWeight);

    // 2. FRESNEL & ÁNH SÁNG
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);
    // Ánh sáng hắt chéo nhẹ thay vì thuần đỉnh (0,1,0) -> đáy cầu vẫn nhận
    // được một phần NdotL, tránh cảm giác đáy "chết cứng" không có sự sống.
    vec3 lightDir = normalize(vec3(0.35, 1.0, 0.25));

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotV = clamp(dot(normal, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - NdotV, 3.0);

    // 2b. FALLOFF THEO TRỤC ĐỨNG CỦA QUẢ CẦU (dùng normal.y, không phụ thuộc
    // UV cầu nên không bị ảnh hưởng bởi méo cực - pole pinching).
    //
    // ĐÂY LÀ FIX CHÍNH cho lỗi "đáy sáng bất thường": ở bản cũ, fresnel rim
    // (pow(fresnel,1.6)*1.8) được CỘNG THẲNG vào baseColor không hề bị chặn
    // trên. Quả cầu vẽ bằng rlBegin(RL_QUADS) không depth-sort theo
    // back-to-front khi alpha-blend, nên tại các tia nhìn xuyên gần đáy,
    // fragment của mặt sau (back face, đang bị occlude bởi front face theo
    // logic depth nhưng vẫn được rasterize và blend do rlDisableDepthMask)
    // cộng rim-glow của CẢ HAI lớp chồng lên nhau -> đáy loé sáng bất ngờ.
    // Falloff theo normal.y giảm rim-glow + alpha ở vùng đáy nên hiệu ứng
    // cộng dồn 2 lớp không còn vượt ngưỡng gây cháy sáng.
    float verticalFalloff = smoothstep(-1.0, 0.15, normal.y);
    verticalFalloff = mix(0.35, 1.0, verticalFalloff); // đáy còn 35%, không tối đen

    // 3. PHỐI MÀU (gradient: đáy tối -> đỉnh sáng)
    vec3 deepWaterColor = vec3(0.01, 0.15, 0.45);
    vec3 lightWaterColor = vec3(0.40, 0.85, 1.00);

    vec3 baseColor = mix(deepWaterColor, lightWaterColor, intensity * 1.5);
    baseColor *= (NdotL * 0.4 + 0.6);
    baseColor *= verticalFalloff;

    // Rim glow: nhân thêm verticalFalloff và CLAMP cứng để dù bị cộng dồn
    // qua nhiều lớp blend (front+back face) cũng không vượt ngưỡng cháy sáng.
    vec3 rimGlow = vec3(0.3, 0.7, 1.0) * pow(fresnel, 1.6) * 1.8 * verticalFalloff;
    baseColor = clamp(baseColor + rimGlow, 0.0, 1.6);

    // 3b. Điểm nhấn: ánh lên màu xoáy ngay tại lõi flow để mắt thấy rõ
    // chuyển động xoắn của nước, không chỉ ẩn trong noise caustics.
    float swirl = length(flowDir);
    baseColor += vec3(0.55, 0.95, 1.0) * swirl * 0.22 * verticalFalloff;

    baseColor *= fragColor.rgb;

    // 4. Alpha: mờ ở tâm mặt, rõ ở rìa; giảm theo verticalFalloff để đáy
    // không dày đặc alpha khi nhìn xuyên 2 lớp mặt cầu cùng lúc.
    float alpha = mix(0.18, 0.75, fresnel) * mix(0.5, 1.0, verticalFalloff);

    finalColor = vec4(baseColor, alpha);
}
