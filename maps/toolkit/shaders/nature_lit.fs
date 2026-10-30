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
    bool isPetalOrLeaf = (fragTexCoord.x >= -0.001) || (hClamped > 0.45);
    float veinMask = 0.0;

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
            float uCentered = abs(fragTexCoord.x * 2.0 - 1.0);
            veinMask = exp(-uCentered * 5.5) * (1.0 - fragTexCoord.y * 0.45);
        } else {
            float centerRidge = 1.0 - 0.045 * (1.0 - abs(clamp(fragTexCoord.x, -1.0, 1.0)));
            vec3 stemLift = mix(vec3(0.98, 1.04, 0.96), vec3(1.08, 1.12, 1.03), hClamped);
            albedo *= stemLift * centerRidge;
        }
    } else {
        // Giải tích gân cánh hoa & cuống hoa 3D (fragTexCoord.x lưu tọa độ ngang [-1..1] của cánh hoa Diamond)
        float uAbs = abs(clamp(fragTexCoord.x, -1.0, 1.0));
        float centerRib = exp(-uAbs * 6.0) * (1.0 - hClamped * 0.45);
        veinMask = centerRib * 0.42;
        float centerRidge = 1.0 - 0.055 * (1.0 - uAbs);
        vec3 stemLift = mix(vec3(0.96, 1.02, 0.95), vec3(1.08, 1.11, 1.04), hClamped);
        albedo *= stemLift * centerRidge * (1.0 - veinMask * 0.14);
    }

    // Chuẩn hóa pháp tuyến 2 mặt bằng 1 lệnh inversesqrt() duy nhất
    vec3 rawN = gl_FrontFacing ? fragNormal : vec3(-fragNormal.x, max(abs(fragNormal.y), 0.28), -fragNormal.z);
    vec3 N = rawN * inversesqrt(max(dot(rawN, rawN), 1e-8));

    vec3 lit = NatureShade(albedo, fragPosition, N, fragHeight, fragTexCoord.x, v_lightSpace, v_staticLightSpace);

    // [XUYÊN SÁNG NGƯỢC NẮNG & VIỀN KIM QUANG CHO HOA DẠI 3D (BOTANICAL SSS & RIM TRANSLUCENCY)]:
    vec3 toView = u_viewPos - fragPosition;
    float distSq = dot(toView, toView);
    if (distSq < 4900.0 && hClamped > 0.12) {
        float invDist = inversesqrt(max(distSq, 1e-6));
        vec3 V = toView * invDist;
        vec3 L = u_lightDir;

        // Màng cánh hoa mỏng ở rìa và đầu cánh (hClamped cao), dày hơn ở gân chính (veinMask) và đài hoa
        float membraneThinness = smoothstep(0.12, 0.88, hClamped) * (1.0 - veinMask * 0.55);
        if (!isPetalOrLeaf) membraneThinness *= 0.28;

        // 1. Tán xạ Mie hướng về mắt khi nhìn ngược nắng
        vec3 sssL = normalize(L + N * 0.34);
        float forwardPhase = clamp(dot(V, -sssL), 0.0, 1.0);
        float mieScatter = (forwardPhase * forwardPhase * forwardPhase) * 1.35 + forwardPhase * 0.35;

        // 2. Truyền sáng qua mặt lưng cánh hoa (Back-face diffuse transmission)
        float backTransmit = max(0.0, dot(-N, L) + 0.22) * 0.48;

        // 3. Viền kim quang Fresnel nơi mép cánh hoa bắt nắng
        float ndv = clamp(abs(dot(N, V)), 0.0, 1.0);
        float fresnelRim = pow(1.0 - ndv, 3.0) * clamp(dot(V, -L) * 0.5 + 0.5, 0.15, 1.0);

        float distFade = 1.0 - smoothstep(1600.0, 4900.0, distSq);
        vec3 sssTint = mix(albedo, albedo * vec3(1.25, 1.12, 0.92) + vec3(0.06, 0.04, 0.02), 0.65);
        vec3 sssLight = sssTint * u_lightColor * ((mieScatter + backTransmit + fresnelRim * 0.55) * membraneThinness * distFade * 0.46);

        lit += sssLight;
    }

    finalColor = vec4(lit, 1.0);
}