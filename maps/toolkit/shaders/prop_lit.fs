#version 330
#include "core/shaders/common/lighting.glsl"
// Đợt E / E2 — spell/effect point lights. Same shared block as the ground and
// the character so a fireball agrees with every surface it stands on.
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"

// ============================================================
// WUXING — prop_lit Fragment Shader (CORE_ISSUES.md Item 36)
//
// PBR-lite material for map terrain/props: diffuse + tangent-space normal
// + roughness-scaled Blinn-Phong specular. Reuses lighting.glsl's
// calcDiffuse/calcSpecular rather than reimplementing them.
//
// Lit by the environment sun/ambient (Environment_GetSunDirection/
// GetSunColor/GetAmbientColor), pushed in once per frame by
// core/prop_lit.c's PropLit_UpdateLighting() — this shader is drawn via
// plain DrawModel()/DrawModelEx() (never wrapped by
// SkillManager_BeginShader), so nothing auto-binds these uniforms; the map
// must call PropLit_UpdateLighting() once per frame before drawing.
//
// Fog (EnvFogConfig) is deliberately NOT implemented here — see
// CORE_ISSUES.md Item 36 "Related, NOT part of this item" (fog is unused
// by every shader in the engine today; an open design question).
// ============================================================

#ifdef GL_ES
precision highp float;
#endif

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec3 fragTangent;
in vec3 fragBitangent;

uniform sampler2D texture0;   // diffuse/albedo (raylib MATERIAL_MAP_DIFFUSE convention)
uniform sampler2D texture2;   // tangent-space normal map (MATERIAL_MAP_NORMAL convention)
uniform sampler2D texture3;   // roughness, single channel .r (MATERIAL_MAP_ROUGHNESS convention)

uniform vec4 colDiffuse;      // raylib standard tint uniform (DrawModelEx's tint param)

uniform vec3 u_lightDir;      // surface -> light, world space (see PropLit_UpdateLighting)
uniform vec3 u_lightColor;    // sun color, normalized 0..1
uniform vec3 u_ambientColor;  // ambient floor color, normalized 0..1
uniform vec3 u_viewPos;       // camera world position

out vec4 finalColor;

void main() {
    vec4 albedo = texture(texture0, fragTexCoord);

    vec3 normalSample = texture(texture2, fragTexCoord).rgb * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(fragTangent), normalize(fragBitangent), normalize(fragNormal));
    vec3 normal = normalize(TBN * normalSample);

    float roughness = clamp(texture(texture3, fragTexCoord).r, 0.04, 1.0);

    vec3 lightDir = normalize(u_lightDir);
    vec3 viewDir  = normalize(u_viewPos - fragPosition);

    // Lambertian term with no floor baked in (ambient is handled separately
    // below via u_ambientColor, which carries its own hue — e.g. cool night
    // ambient vs warm sun — rather than degenerating through calcDiffuse's
    // single-scalar ambient floor).
    float diff = calcDiffuse(normal, lightDir, 0.0);

    // Roughness scales shininess: rough surfaces -> wide dim highlight,
    // smooth surfaces -> tight bright highlight.
    float shininess    = mix(128.0, 8.0, roughness);
    float specStrength = mix(0.05, 0.6, 1.0 - roughness);
    float spec = calcSpecular(normal, lightDir, viewDir, shininess) * specStrength;

    float shadow = MapShadowVisibility(fragPosition, normal, lightDir);
    vec3 lit = albedo.rgb * (u_ambientColor + diff * u_lightColor * shadow)
             + spec * u_lightColor * shadow;

    // `normal` is the TBN-perturbed normal built from fragNormal, which prop_lit.vs
    // derives via mat3(matModel) — the same model*view matrix the lights are
    // uploaded through, so it is already in their space and needs no conversion.
    lit += VFXLights_Accumulate(fragPosition, normal, albedo.rgb);

    finalColor = vec4(lit, albedo.a) * colDiffuse;
}
