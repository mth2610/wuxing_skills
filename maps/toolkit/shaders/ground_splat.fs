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

    // Terrain textures supply neutral detail; the material tint owns biome
    // colour. Two rotated scales suppress obvious photographic repetition.
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 colorGrass = texture(texGrass, tiledUV);
    vec2 broadUV = vec2(tiledUV.y * 0.43 + 17.0, -tiledUV.x * 0.43 + 9.0);
    vec3 broadGrass = texture(texGrass, broadUV).rgb;
    vec4 colorPath  = texture(texPath, tiledUV);
    float fineLuma = dot(colorGrass.rgb, vec3(0.2126, 0.7152, 0.0722));
    float broadLuma = dot(broadGrass, vec3(0.2126, 0.7152, 0.0722));
    float detail = 0.93 + (fineLuma - 0.50) * 0.24
                        + (broadLuma - 0.50) * 0.12;
    detail = clamp(detail, 0.82, 1.06);
    vec4 grassDetail = vec4(vec3(detail), colorGrass.a);
    vec4 mixedTex = mix(colorPath, grassDetail, mask);
    float macroA = sin(fragPosition.x * 0.071 + fragPosition.z * 0.047);
    float macroB = sin(fragPosition.x * -0.039 + fragPosition.z * 0.083 + 1.7);
    mixedTex.rgb *= 0.985 + 0.018 * macroA + 0.012 * macroB;

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
    float ambientVisibility = mix(0.94, 1.0, shadow);
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
