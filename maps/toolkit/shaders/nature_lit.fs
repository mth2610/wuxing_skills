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
in float v_macroPatch;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    float hClamped = clamp(fragHeight, 0.0, 1.0);

    // [VULKAN / SPIR-V FIX & ZERO-DISCARD PATH]:
    // - Khi u_useTexture == 0 (Hoa 3D đa giác Zero-Discard): Không tốn lệnh lấy mẫu texture hay discard,
    //   giữ 100% tốc độ phần cứng Early-Z / Hi-Z!
    // - Khi u_useTexture != 0: Tính dFdx/dFdy trong nhánh đồng nhất trước khi kiểm tra fragTexCoord.x.
    if (u_useTexture != 0) {
        vec2 uvDx = dFdx(fragTexCoord);
        vec2 uvDy = dFdy(fragTexCoord);
        if (fragTexCoord.x >= 0.0) {
            vec4 texel = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
            float uvGrad = dot(abs(uvDx) + abs(uvDy), vec2(0.5));
            float boostedAlpha = texel.a + clamp(uvGrad * 0.65, 0.0, 0.22);
            if (boostedAlpha < u_alphaCutoff) discard;
            albedo *= texel.rgb;
        } else {
            float centerRidge = 1.0 - 0.045 * (1.0 - abs(clamp(fragTexCoord.x, -1.0, 1.0)));
            vec3 stemLift = mix(vec3(0.98, 1.04, 0.96), vec3(1.08, 1.12, 1.03), hClamped);
            albedo *= stemLift * centerRidge;
        }
    } else {
        float uAbs = abs(clamp(fragTexCoord.x, -1.0, 1.0));
        float centerRidge = 1.0 - 0.055 * (1.0 - uAbs);
        vec3 stemLift = mix(vec3(0.96, 1.02, 0.95), vec3(1.08, 1.11, 1.04), hClamped);
        albedo *= stemLift * centerRidge;
    }

    // Truyền trực tiếp fragNormal (chưa lật dấu) vào NatureShade vì NatureShade đã xử lý
    // gl_FrontFacing, gân cánh hoa (veinMask), SSS hai thành phần và bóng đổ (shadow * cloudVis).
    vec3 lit = NatureShade(albedo, fragPosition, fragNormal, hClamped, fragTexCoord.x,
                           v_lightSpace, v_staticLightSpace);

    // Ánh kim động khi hoa/lá nghiêng mạnh theo luồng gió (windSheen truyền qua fragColor.a)
    float windSheen = fragColor.a;
    if (windSheen > 0.001) {
        lit += u_lightColor * vec3(1.05, 1.02, 0.88) * (windSheen * 0.16 * hClamped);
    }

    finalColor = vec4(lit, 1.0);
}