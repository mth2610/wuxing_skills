#version 330

// Đợt E / E2 — VFX point lights. The paved path is what a caster actually
// stands on, so an effect that lights the grass but not the stone underfoot
// still reads as pasted on — which is exactly how it looked before this.
#include "core/shaders/common/vfx_lights.glsl"

in vec2 fragTexCoord;
in vec3 fragPosition;   // world space, from path_blend.vs (E2)
in vec3 fragTangent;
in vec3 fragBitangent;
in vec3 fragNormal;

uniform sampler2D texture0; // Texture đường
uniform sampler2D texture2; // optional tangent-space normal
uniform sampler2D texture3; // optional roughness
uniform vec2 tiling;
uniform vec4 colDiffuse;
uniform int u_hasSurfaceMaps;

// Thông số ánh sáng
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambientColor;
uniform vec3 viewPos;

out vec4 finalColor;

// --- HÀM TẠO NHIỄU (Value Noise) ---
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float noise(vec2 st) {
    vec2 i = floor(st); vec2 f = fract(st);
    float a = random(i); float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0)); float d = random(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main()
{
    // Đọc màu texture lặp lại của con đường
    vec2 tiledUV = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, tiledUV);
    vec2 rotatedUV = vec2(-tiledUV.y * 0.47 + 13.7, tiledUV.x * 0.47 + 8.3);
    texColor.rgb = mix(texColor.rgb, texture(texture0, rotatedUV).rgb, 0.16);

    // --- TẠO LỀ ĐƯỜNG MỜ & MÉO MÓ ---
    // Dùng nhiễu (noise) để phá vỡ đường thẳng hình học của Mesh
    float n = noise(fragPosition.xz * 1.35);
    float edgeDistance = min(fragTexCoord.y, 1.0 - fragTexCoord.y);
    float edgeLimit = 0.055 + (n - 0.5) * 0.075;
    float coverage = smoothstep(edgeLimit, edgeLimit + 0.085, edgeDistance);
    if (coverage < 0.52) discard;
    float edgeWear = 1.0 - smoothstep(0.08, 0.31, edgeDistance);
    vec3 edgeTint = texColor.rgb * vec3(0.61, 0.69, 0.53);
    texColor.rgb = mix(texColor.rgb, edgeTint, edgeWear * (0.14 + n * 0.18));

    // --- ÁNH SÁNG ---
    vec3 normal = normalize(fragNormal);
    float roughness = 0.82;
    if (u_hasSurfaceMaps != 0) {
        vec3 tangentNormal = texture(texture2, tiledUV).xyz * 2.0 - 1.0;
        normal = normalize(fragTangent * tangentNormal.x
                         + fragBitangent * tangentNormal.y
                         + fragNormal * tangentNormal.z);
        roughness = clamp(texture(texture3, tiledUV).r, 0.22, 1.0);
    }
    vec3 light = vec3(0.0, 1.0, 0.0);
    if (length(lightDir) > 0.1) light = normalize(-lightDir);
    float NdotL = max(dot(normal, light), 0.0);
    
    vec4 actualAmbient = ambientColor.a == 0.0 ? vec4(0.4, 0.4, 0.4, 1.0) : ambientColor;
    vec4 actualLight = lightColor.a == 0.0 ? vec4(1.0, 1.0, 1.0, 1.0) : lightColor;
    vec4 totalLight = actualAmbient + (actualLight * NdotL);

    // Alpha KHÔNG được nhân totalLight.a (tới ~2): trên scene buffer HDR float
    // (Đợt G) src alpha không bị kẹp [0,1] → blend hoá điên, biển mây lòi qua
    // đường. Alpha chỉ = texture.a * tint.a * độ-mờ-lề (đều ≤1); lit chỉ ở rgb.
    vec3 pathLit = (texColor * colDiffuse * totalLight).rgb;
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 halfDir = normalize(light + viewDir);
    float specular = pow(max(dot(normal, halfDir), 0.0), mix(54.0, 10.0, roughness));
    pathLit += actualLight.rgb * specular * (1.0 - roughness) * 0.18;
    pathLit += VFXLights_AccumulateFlat(fragPosition, (texColor * colDiffuse).rgb);

    finalColor = vec4(pathLit, 1.0);
}
