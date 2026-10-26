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

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;

    // [VULKAN / SPIR-V FIX]: Tính đạo hàm dFdx/dFdy trong nhánh đồng nhất (u_useTexture là uniform)
    // trước khi kiểm tra điều kiện biến thiên theo đỉnh (fragTexCoord.x >= 0.0) và dùng textureGrad
    // để tránh lỗi lấy mẫu mipmap trong non-uniform control flow tại ranh giới thân-cánh hoa.
    if (u_useTexture != 0) {
        vec2 uvDx = dFdx(fragTexCoord);
        vec2 uvDy = dFdy(fragTexCoord);
        if (fragTexCoord.x >= 0.0) {
            vec4 texel = textureGrad(texture0, fragTexCoord, uvDx, uvDy);
            // Giữ độ phủ alpha ở các bậc mipmap xa để hoa/bông cỏ không bị mỏng đi
            float uvGrad = dot(abs(uvDx) + abs(uvDy), vec2(0.5));
            float boostedAlpha = texel.a + clamp(uvGrad * 0.65, 0.0, 0.22);
            if (boostedAlpha < u_alphaCutoff) discard;
            albedo *= texel.rgb;
        } else {
            float hClamped = clamp(fragHeight, 0.0, 1.0);
            float centerRidge = 1.0 - 0.045 * (1.0 - abs(clamp(fragTexCoord.x, -1.0, 1.0)));
            vec3 stemLift = mix(vec3(0.98, 1.04, 0.96), vec3(1.08, 1.12, 1.03), hClamped);
            albedo *= stemLift * centerRidge;
        }
    } else {
        // [TỐI ƯU VISUAL]: Làm dịu hệ số stemLift để thân cỏ/hoa hòa vào nền đất mượt hơn, không bị chói vàng
        float hClamped = clamp(fragHeight, 0.0, 1.0);
        float centerRidge = 1.0 - 0.045 * (1.0 - abs(clamp(fragTexCoord.x, -1.0, 1.0)));
        vec3 stemLift = mix(vec3(0.98, 1.04, 0.96), vec3(1.08, 1.12, 1.03), hClamped);
        albedo *= stemLift * centerRidge;
    }

    // [TỐI ƯU ALU]: Lật hướng pháp tuyến mặt sau TRƯỚC khi chuẩn hóa an toàn bằng inversesqrt()
    vec3 rawN = gl_FrontFacing ? fragNormal : vec3(-fragNormal.x, max(abs(fragNormal.y), 0.28), -fragNormal.z);
    vec3 N = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));

    finalColor = vec4(NatureShade(albedo, fragPosition, N, fragHeight, fragTexCoord.x, v_lightSpace, v_staticLightSpace), 1.0);
}
