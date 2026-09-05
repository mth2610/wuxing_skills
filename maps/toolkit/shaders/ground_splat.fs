#version 330

// Đợt E / E2 — VFX point lights. Shared block so the ground agrees with the
// characters and the smoke about where the light is; a private copy of the
// falloff drifts the moment one of them is edited.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // project surface space; see ground_splat.vs
in vec3 fragNormal;

uniform vec4 colDiffuse;
uniform sampler2D texture0; // Splatmap
uniform sampler2D texGrass; // Ảnh cỏ
uniform sampler2D texPath;  // Ảnh đường đi

uniform vec2 tiling;

uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;

out vec4 finalColor;

void main()
{
    // 1. TẠM THỜI gán mask = 1.0 để ép hiển thị 100% CỎ. 
    // Khi nào bạn thiết kế xong ảnh Splatmap chuẩn thì mở comment dòng dưới và xóa dòng mask = 1.0 đi.
    // float mask = texture(texture0, fragTexCoord).r;
    float mask = 1.0; 

    // Lấy màu Texture
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 colorGrass = texture(texGrass, tiledUV);
    vec2 broadUV = vec2(tiledUV.y * 0.43 + 17.0, -tiledUV.x * 0.43 + 9.0);
    colorGrass.rgb = mix(colorGrass.rgb, texture(texGrass, broadUV).rgb, 0.34);
    vec4 colorPath  = texture(texPath, tiledUV);
    vec4 mixedTex = mix(colorPath, colorGrass, mask);

    // Keep the terrain as the low-frequency base of the biome instead of a
    // saturated photographic carpet competing with the geometry above it.
    // The slow world-space variation also breaks visible texture tiling.
    float groundLuma = dot(mixedTex.rgb, vec3(0.2126, 0.7152, 0.0722));
    mixedTex.rgb = mix(vec3(groundLuma), mixedTex.rgb, 0.58);
    mixedTex.rgb = mix(mixedTex.rgb, vec3(0.20, 0.29, 0.16), 0.24);
    float macroTone = 0.95 + 0.045 * sin(fragPosition.x * 0.105 + fragPosition.z * 0.073);
    mixedTex.rgb *= macroTone;

    // Use the actual heightmap normal so slopes and lake banks respond to the
    // sun instead of receiving one constant brightness across the whole map.
    vec3 normal = normalize(fragNormal);

    // 3. Bảo vệ hướng sáng (Phòng trường hợp vector lightDir bị bằng 0 từ code C)
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) {
        light = normalize(-lightDir);
    }
    
    float NdotL = max(dot(normal, light), 0.0);
    
    // 4. Bảo vệ ánh sáng môi trường (Nếu C gửi sang màu Đen (Alpha=0), tự động dùng màu xám sáng)
    vec4 actualAmbient = ambientColor;
    if (actualAmbient.a == 0.0) {
        actualAmbient = vec4(0.4, 0.4, 0.4, 1.0); 
    }
    
    vec4 actualLight = lightColor;
    if (actualLight.a == 0.0) {
        actualLight = vec4(1.0, 1.0, 1.0, 1.0);
    }

    float shadow = MapShadowVisibility(fragPosition, normal, light);
    float skyWeight = normal.y * 0.5 + 0.5;
    vec3 skyAmbient = actualAmbient.rgb * vec3(1.08, 1.12, 1.20);
    vec3 groundBounce = actualAmbient.rgb * vec3(0.47, 0.40, 0.32);
    vec3 ambient = mix(groundBounce, skyAmbient, skyWeight);
    // Preserve sky fill, but do not let a bright environment erase the
    // shape-accurate shadow-map silhouette as it did after the sunset pass.
    // Outdoor vegetation shadows lose direct sun but retain cool sky fill;
    // crushing ambient here turns every fine silhouette into a black decal.
    float ambientVisibility = mix(0.74, 1.0, shadow);
    vec3 totalLight = ambient * ambientVisibility
                    + actualLight.rgb * NdotL * shadow;

    // Xuất màu cuối — alpha PHẢI = 1.0 (mặt đất đục). totalLight.a có thể tới ~2
    // (ambient.a + sun.a*NdotL); trên scene buffer HDR float (Đợt G) alpha
    // nguồn KHÔNG bị kẹp [0,1] như RGBA8 cũ → src alpha 2.0 làm blend
    // (GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA) hoá điên: dst = 2*đất - mây → biển
    // mây dưới đảo lòi/cộng màu xuyên qua nền. Ép alpha 1 để nền luôn đục.
    vec3 groundLit = mixedTex.rgb * colDiffuse.rgb * totalLight;
    // Flat variant: the splat plane has no per-vertex normal worth trusting, and
    // a floor faces up.
    groundLit += VFXLights_AccumulateFlat(fragPosition, (mixedTex * colDiffuse).rgb);

    finalColor = vec4(groundLit, 1.0);
}
