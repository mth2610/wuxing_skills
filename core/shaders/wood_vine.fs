#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/wood_fx.glsl"

uniform float u_growth;
uniform float u_wither;
uniform float u_sapPulseFreq;
uniform float u_sapPulseSpeed;
uniform vec4  u_baseColor;     // Dark bark tone (e.g. 0.16, 0.10, 0.06)
uniform vec4  u_foliageColor;  // Lush emerald green (e.g. 0.18, 0.65, 0.28)
uniform vec4  u_sapColor;      // Luminous jade sap (HDR) (e.g. 0.40, 1.40, 0.65)

void main()
{
    float arc = clamp(fragTexCoord.y, 0.0, 1.0);

    // Discard fragment if not yet reached by growth front
    if (u_growth < 0.999) {
        if (arc > u_growth) {
            discard;
        }
    }

    vec3 normal = normalize(fragNormal);
    // View direction in view space (camera is at origin)
    vec3 viewDir = normalize(-fragPosition);
    vec3 lightDir = (length(u_lightDir) > 0.001) ? normalize(u_lightDir) : normalize(vec3(0.4, 0.8, 0.4));

    // 1. Procedural bark fibrous striations & deep wood grain ridges
    float barkRibs = sin(fragTexCoord.x * 32.0) * 0.5 + 0.5;
    float barkFibers = sin(fragTexCoord.y * 75.0 + sin(fragTexCoord.x * 16.0) * 3.5) * 0.5 + 0.5;
    float woodNoise = barkRibs * 0.6 + barkFibers * 0.4;

    // 2. Base albedo: Ancient weathered bark with moss fissures and fresh tip
    vec3 barkDeep = u_baseColor.rgb * 0.65;                       // Dark umber crevice
    vec3 barkRidge = u_baseColor.rgb * (1.10 + woodNoise * 0.25); // Fibrous ridge
    vec3 barkMoss = vec3(0.16, 0.22, 0.10);                      // Ancient lichen patina
    vec3 agedBark = mix(barkDeep, mix(barkRidge, barkMoss, barkRibs * 0.4), pow(barkRibs, 1.3));

    // Only the emerging tip (top 20%) transitions to fresh vegetative tender shoot
    float tipTender = smoothstep(0.70, 0.98, arc);
    vec3 lushAlbedo = mix(agedBark, u_foliageColor.rgb * (0.85 + woodNoise * 0.3), tipTender);

    // 3. Wither transformation: Green -> Autumn Gold -> Dead Timber
    vec3 albedo = Wood_WitherAlbedo(lushAlbedo, u_wither, arc);

    // 4. Wrap diffuse lighting + rough bark specular sheen
    float NdotL = dot(normal, lightDir);
    float wrapDiffuse = clamp((NdotL + 0.35) / 1.35, 0.0, 1.0);
    
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 22.0) * 0.25 * (1.0 - u_wither);

    // Ambient floor: gentle forest bounce
    vec3 ambient = albedo * vec3(0.24, 0.26, 0.22);
    vec3 litColor = ambient + albedo * (wrapDiffuse * 0.85) + vec3(spec);

    // 5. Biological sap pulse — confined inside bark fissures and cracks
    float sapFreq = (u_sapPulseFreq > 0.1) ? u_sapPulseFreq : 4.0;
    float sapSpeed = (u_sapPulseSpeed > 0.1) ? u_sapPulseSpeed : 2.5;
    float sapFactor = Wood_SapPulse(arc, u_time, sapFreq, sapSpeed);
    sapFactor *= (1.0 - clamp(u_wither * 1.3, 0.0, 1.0));

    // Sap only shines through deep bark fissures, never as a flat wash
    float fissureVein = smoothstep(0.68, 0.94, barkFibers * 0.7 + (1.0 - barkRibs) * 0.3);
    vec3 sapGlow = u_sapColor.rgb * (sapFactor * fissureVein * 3.5);

    // 6. Subtle botanical velvet rim lighting (subsurface peach-fuzz)
    float fresnel = pow(clamp(1.0 - max(dot(normal, viewDir), 0.0), 0.0, 1.0), 3.2);
    vec3 rimLight = mix(vec3(0.12, 0.16, 0.09), vec3(0.50, 0.75, 0.40), tipTender) * (fresnel * 0.30);

    // 7. Final HDR color composition
    vec3 finalRGB = litColor + sapGlow + rimLight;
    float alpha = clamp(u_baseColor.a, 0.0, 1.0);

    // Optional wither dissolve fade at final stage
    if (u_wither > 0.90) {
        alpha *= (1.0 - (u_wither - 0.90) * 10.0);
    }

    finalColor = vec4(finalRGB, alpha);
}
