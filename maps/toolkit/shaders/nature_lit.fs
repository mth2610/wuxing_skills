#version 330
#include "core/shaders/common/vfx_lights.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;

uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec3 u_viewPos;
uniform vec4 colDiffuse;
uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    vec3 viewDir = normalize(u_viewPos - fragPosition);
    vec3 n = normalize(fragNormal);
    if (dot(n, viewDir) < 0.0) n = -n;
    // Foliage normals are deliberately biased upward. Horizon used custom
    // canopy normals for the same reason: literal card normals reveal the
    // geometry and turn half the field black under grazing light.
    n = normalize(mix(n, vec3(0.0, 1.0, 0.0), 0.58));
    float wrappedDiffuse = clamp((dot(n, u_lightDir) + 0.42) / 1.42, 0.0, 1.0);
    float transmission = pow(max(dot(-u_lightDir, viewDir), 0.0), 2.0) * 0.24;
    float horizon = 0.84 + 0.16 * max(n.y, 0.0);
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;
    if (u_useTexture != 0 && fragTexCoord.x >= 0.0) {
        vec4 texel = texture(texture0, fragTexCoord);
        if (texel.a < u_alphaCutoff) discard;
        float texLuma = max(dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.12);
        albedo *= mix(0.76, 1.16, texLuma);
    }
    vec3 ambientFloor = max(u_ambientColor, vec3(0.24, 0.27, 0.22));
    vec3 lit = albedo * (ambientFloor * horizon + u_lightColor * wrappedDiffuse * 0.72);
    lit += albedo * u_lightColor * transmission;
    lit += albedo * fragHeight * 0.025;
    lit += VFXLights_Accumulate(fragPosition, n, albedo);
    finalColor = vec4(lit, 1.0);
}
