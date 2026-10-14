#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform float u_grassTipSoftening;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    vec3 lit = GrassShade(albedo, fragPosition, fragNormal, fragHeight,
                          fragTexCoord.x, v_lightSpace, v_staticLightSpace);

    // 1. Tối ưu Tip AA: dùng fwidth trực tiếp để giữ độ sắc nét ở gần và mượt ở xa
    float tipPixels = max(fwidth(fragHeight) * 1.5, 0.012);
    float tip = 1.0 - smoothstep(0.0, tipPixels, 1.0 - fragHeight);

    // 2. Tối ưu Thin-blade AA: Loại bỏ phép chia (2.0 / fw) bằng cách xét trực tiếp đạo hàm fw.
    // Khi widthPixels nằm trong [0.75, 1.75], fw = 2.0 / widthPixels sẽ nằm trong [1.1428, 2.6667].
    float fw = fwidth(fragTexCoord.x);
    float thinBlade = smoothstep(1.1428, 2.6667, fw);

    // 3. Visual: Dùng max() thay vì cộng dồn để tránh đầu cỏ bị tối đen (double-darkening),
    // đồng thời giới hạn thinBlade theo fragHeight để gốc cỏ không bị mất độ tương phản.
    float heightFade = smoothstep(0.15, 0.85, fragHeight);
    float aaAttenuation = max(0.28 * tip, 0.22 * thinBlade * heightFade);

    lit *= 1.0 - u_grassTipSoftening * aaAttenuation;
    finalColor = vec4(lit, 1.0);
}