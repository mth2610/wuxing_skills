#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor; // .rgb = blade color, .a = windSheen
in float fragHeight;
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform float u_grassTipSoftening;

out vec4 finalColor;

void main()
{
    // 1. Chuẩn hóa nhanh pháp tuyến và tạo độ cong khối 3D ngang thân lá (cylindrical cross-section)
    float u = fragTexCoord.x;
    float absU = abs(u);
    vec3 baseNormal = gl_FrontFacing ? fragNormal : -fragNormal;
    vec3 sideAxis = normalize(vec3(-baseNormal.z, 0.0, baseNormal.x));
    vec3 curvedNormal = normalize(baseNormal + sideAxis * (u * 0.22));

    // 2. Tạo hiệu ứng gân lá (midrib) nhẹ và độ chuyển sắc tự nhiên từ gốc lên ngọn
    float midrib = 1.0 - absU * 0.12;
    vec3 albedo = fragColor.rgb * colDiffuse.rgb * midrib;

    vec3 lit = GrassShade(albedo, fragPosition, curvedNormal, fragHeight,
                          u, v_lightSpace, v_staticLightSpace);

    // 3. Cộng hưởng ánh sáng sóng gió (Wind-Wave Sheen) truyền từ fragColor.a
    float windSheen = fragColor.a;
    lit *= 1.0 + windSheen * (0.14 + 0.10 * fragHeight);

    // 4. Tối ưu Tip AA + Lateral Edge AA + Thin-blade AA (thay smoothstep bằng saturate/clamp nhanh)
    float fwU = fwidth(u);
    float fwH = fwidth(fragHeight);

    // Làm mềm chóp lá
    float tipPixels = max(fwH * 1.5, 0.012);
    float tip = 1.0 - clamp((1.0 - fragHeight) / tipPixels, 0.0, 1.0);

    // Làm mềm 2 mép dọc của lá cỏ để khử răng cưa/lấp lánh (shimmering) ở khoảng cách trung bình
    float edgeSoftness = clamp((absU - (1.0 - fwU * 0.85)) / max(fwU, 0.001), 0.0, 1.0);

    // Làm dịu các lá cỏ mỏng dưới chuẩn sub-pixel ở xa (fwU trong khoảng [1.1428, 2.6667])
    float thinBlade = clamp((fwU - 1.1428) * 0.6562, 0.0, 1.0);
    float heightFade = clamp((fragHeight - 0.15) * 1.4285, 0.0, 1.0);

    float aaAttenuation = max(max(0.28 * tip, 0.18 * edgeSoftness * heightFade), 0.22 * thinBlade * heightFade);

    lit *= 1.0 - u_grassTipSoftening * aaAttenuation;
    finalColor = vec4(lit, 1.0);
}