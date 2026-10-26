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
in float v_macroPatch;

uniform float u_grassTipSoftening;

out vec4 finalColor;

void main()
{
    // Khôi phục tọa độ ngang [-1..1] không cần nhánh rẽ tại điểm nối thân-ngọn (0.76)
    float tipScale = clamp((1.0 - fragHeight) * 4.166667, 0.08, 1.0);
    float uNorm = clamp(fragTexCoord.x / tipScale, -1.0, 1.0);

    // [TỐI ƯU VISUAL & ALU]: Khi nhìn mặt lưng của lá cỏ uốn cong (!gl_FrontFacing),
    // giữ thành phần pháp tuyến trục Y hướng lên bầu trời (>= 0.30) thay vì đảo âm (-fragNormal.y),
    // sau đó chuẩn hóa hợp nhất cả mặt trước và mặt sau chỉ bằng 1 lệnh inversesqrt() duy nhất.
    vec3 rawN = gl_FrontFacing
        ? fragNormal
        : vec3(-fragNormal.x, max(abs(fragNormal.y), 0.30), -fragNormal.z);
    vec3 baseNormal = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));

    vec3 albedo = fragColor.rgb * colDiffuse.rgb;

    vec3 lit = GrassShade(albedo, fragPosition, baseNormal, fragHeight,
                          uNorm, v_lightSpace, v_staticLightSpace, v_macroPatch);

    // Dynamic Wind-Wave Glint & Sheen (làm dịu ánh vàng để không gây bệt màu cỏ)
    float windSheen = fragColor.a;
    if (windSheen > 0.001) {
        float archMask = smoothstep(0.14, 0.46, fragHeight) * (1.0 - smoothstep(0.72, 0.95, fragHeight));
        vec3 sheenCol = u_lightColor * vec3(1.06, 1.04, 0.82) * (windSheen * 0.24 * archMask);
        lit += sheenCol;
        lit *= 1.0 + windSheen * 0.08;
    }

    finalColor = vec4(lit, 1.0);
}
