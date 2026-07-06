#include "material_system.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "rlgl.h"
#include "raymath.h"

static void Material_FetchLocs(EffectMaterial *mat) {
    mat->uTimeLoc = GetShaderLocation(mat->shader, "u_time");
    mat->uDissolveLoc = GetShaderLocation(mat->shader, "u_dissolve");
    mat->uBaseColorLoc = GetShaderLocation(mat->shader, "u_baseColor");
    mat->uTranslucencyLoc = GetShaderLocation(mat->shader, "u_translucency");
    mat->uRimStrengthLoc = GetShaderLocation(mat->shader, "u_rimStrength");
    mat->uFresnelPowerLoc = GetShaderLocation(mat->shader, "u_fresnelPower");
    mat->uEmissiveIntensityLoc = GetShaderLocation(mat->shader, "u_emissiveIntensity");
    mat->uDistortionStrengthLoc = GetShaderLocation(mat->shader, "u_distortionStrength");
    mat->uHasTexture1Loc = GetShaderLocation(mat->shader, "u_hasTexture1");
    mat->uTexture1Loc = GetShaderLocation(mat->shader, "texture1");
}

void MaterialSystem_Init(void) {
    // Mồi trước shader dùng chung
    ResourceManager_LoadShader("core/shaders/effect_material.vs", "core/shaders/effect_material.fs");
}

void MaterialSystem_Unload(void) {
    // Để trống, resource_manager tự lo dọn dẹp
}

EffectMaterial Material_Get(MaterialPreset preset) {
    EffectMaterialParams p = {0};

    switch (preset) {
        case MAT_FIRE:
            p.baseColor = ELEMENT_COLOR_FIRE;
            p.rimStrength = 1.2f;
            p.fresnelPower = 3.0f;
            p.emissiveIntensity = 1.5f;
            p.distortionStrength = 0.4f;
            p.translucency = 0.0f;
            break;
        case MAT_ICE:
            p.baseColor = (Color){170, 220, 255, 150}; // pale blue (Alpha 150 để trong suốt)
            p.rimStrength = 1.5f;
            p.fresnelPower = 5.0f;
            p.emissiveIntensity = 0.5f;
            p.distortionStrength = 0.0f;
            p.translucency = 0.6f;
            p.texture1 = ResourceManager_LoadTexture("assets/textures/tex_ice_crystal.png");
            break;
        case MAT_WATER:
            p.baseColor = ELEMENT_COLOR_WATER;
            p.rimStrength = 1.0f;
            p.fresnelPower = 4.0f;
            p.emissiveIntensity = 0.6f;
            p.distortionStrength = 0.25f;
            p.translucency = 0.85f;
            break;
        case MAT_PORTAL:
            p.baseColor = ELEMENT_COLOR_TAIJI;
            p.rimStrength = 2.0f;
            p.fresnelPower = 2.0f;
            p.emissiveIntensity = 2.0f;
            p.distortionStrength = 0.6f;
            p.translucency = 0.3f;
            break;
        case MAT_ROCK:
            p.baseColor = (Color){150, 110, 80, 255}; // Màu đá đất nung
            p.rimStrength = 0.3f;
            p.fresnelPower = 2.0f;
            p.distortionStrength = 0.0f;
            p.translucency = 0.0f;
            p.texture1 = ResourceManager_LoadTexture("assets/textures/tex_rock_albedo.png");
            break;
        case MAT_METAL:
            p.baseColor = ELEMENT_COLOR_METAL;
            p.rimStrength = 1.8f;
            p.fresnelPower = 6.0f;
            p.emissiveIntensity = 1.0f;
            p.distortionStrength = 0.08f;
            p.translucency = 0.2f;
            break;
        case MAT_GLASS:
            p.baseColor = (Color){200, 230, 255, 100};
            p.rimStrength = 1.5f;
            p.fresnelPower = 4.0f;
            p.emissiveIntensity = 0.2f;
            p.distortionStrength = 0.1f;
            p.translucency = 0.9f;
            break;
        default:
            break;
    }

    EffectMaterial mat = Material_LoadCustom(p);
    mat.preset = preset;
    return mat;
}

EffectMaterial Material_LoadCustom(EffectMaterialParams params) {
    EffectMaterial mat = {0};
    mat.preset = MAT_CUSTOM;
    mat.shader = ResourceManager_LoadShader("core/shaders/effect_material.vs",
                                           "core/shaders/effect_material.fs");
    Material_FetchLocs(&mat);
    mat.params = params;
    return mat;
}

void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val) {
    int loc = GetShaderLocation(mat->shader, uniformName);
    if (loc >= 0) {
        SetShaderValue(mat->shader, loc, &val, SHADER_UNIFORM_FLOAT);
    }
}

void Material_Begin(EffectMaterial mat) {
    // Đảm bảo an toàn Raylib Batching Hazard: xả sạch batch trước khi thay đổi trạng thái shader/texture
    rlDrawRenderBatchActive();
    
    SkillManager_BeginShader(mat.shader);
    float time = (float)GetTime();
    if (mat.uTimeLoc >= 0) {
        SetShaderValue(mat.shader, mat.uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    }
    if (mat.uBaseColorLoc >= 0) {
        Vector4 baseColorVec = ColorNormalize(mat.params.baseColor);
        SetShaderValue(mat.shader, mat.uBaseColorLoc, &baseColorVec, SHADER_UNIFORM_VEC4);
    }
    if (mat.uTranslucencyLoc >= 0)
        SetShaderValue(mat.shader, mat.uTranslucencyLoc, &mat.params.translucency, SHADER_UNIFORM_FLOAT);
    if (mat.uRimStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uRimStrengthLoc, &mat.params.rimStrength, SHADER_UNIFORM_FLOAT);
    if (mat.uFresnelPowerLoc >= 0)
        SetShaderValue(mat.shader, mat.uFresnelPowerLoc, &mat.params.fresnelPower, SHADER_UNIFORM_FLOAT);
    if (mat.uEmissiveIntensityLoc >= 0)
        SetShaderValue(mat.shader, mat.uEmissiveIntensityLoc, &mat.params.emissiveIntensity, SHADER_UNIFORM_FLOAT);
    if (mat.uDistortionStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uDistortionStrengthLoc, &mat.params.distortionStrength, SHADER_UNIFORM_FLOAT);
    if (mat.uHasTexture1Loc >= 0) {
        int hasTexture1 = mat.params.texture1.id != 0;
        SetShaderValue(mat.shader, mat.uHasTexture1Loc, &hasTexture1, SHADER_UNIFORM_INT);
    }
    if (mat.uTexture1Loc >= 0 && mat.params.texture1.id != 0) {
        rlSetTexture(mat.params.texture1.id);
        SetShaderValueTexture(mat.shader, mat.uTexture1Loc, mat.params.texture1);
    }
}

void Material_End(void) {
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    SkillManager_EndShader();
}

CrystalMaterial CrystalMaterial_Load(CrystalMaterialParams params)
{
    CrystalMaterial mat = {0};
    mat.shader = ResourceManager_LoadShader("core/shaders/crystal.vs", "core/shaders/crystal.fs");
    mat.params = params;

    if (mat.params.texture1.id == 0)
    {
        mat.params.texture1 = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    }

    mat.uBaseColorLoc = GetShaderLocation(mat.shader, "u_baseColor");
    mat.uEdgeColorLoc = GetShaderLocation(mat.shader, "u_edgeColor");
    mat.uFresnelPowerLoc = GetShaderLocation(mat.shader, "u_fresnelPower");
    mat.uRimStrengthLoc = GetShaderLocation(mat.shader, "u_rimStrength");
    mat.uRefractionLoc = GetShaderLocation(mat.shader, "u_refraction");
    mat.uSparkleLoc = GetShaderLocation(mat.shader, "u_sparkle");
    mat.uCrackLoc = GetShaderLocation(mat.shader, "u_crack");
    mat.uEmissionLoc = GetShaderLocation(mat.shader, "u_emission");
    mat.uThicknessLoc = GetShaderLocation(mat.shader, "u_thickness");
    mat.uDissolveLoc = GetShaderLocation(mat.shader, "u_dissolve");
    mat.uTexture1Loc = GetShaderLocation(mat.shader, "texture1");
    mat.uTimeLoc = GetShaderLocation(mat.shader, "u_time");

    return mat;
}

void CrystalMaterial_Begin(CrystalMaterial mat)
{
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(mat.shader);

    float time = (float)GetTime();
    if (mat.uTimeLoc >= 0)
    {
        SetShaderValue(mat.shader, mat.uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    }

    if (mat.uBaseColorLoc >= 0)
    {
        Vector4 baseColorVec = ColorNormalize(mat.params.baseColor);
        SetShaderValue(mat.shader, mat.uBaseColorLoc, &baseColorVec, SHADER_UNIFORM_VEC4);
    }

    if (mat.uEdgeColorLoc >= 0)
    {
        Vector4 edgeColorVec = ColorNormalize(mat.params.edgeColor);
        SetShaderValue(mat.shader, mat.uEdgeColorLoc, &edgeColorVec, SHADER_UNIFORM_VEC4);
    }

    if (mat.uFresnelPowerLoc >= 0)
    {
        float fresnelPower = 8.0f - mat.params.roughness * 7.0f;
        if (fresnelPower < 1.0f) fresnelPower = 1.0f;
        SetShaderValue(mat.shader, mat.uFresnelPowerLoc, &fresnelPower, SHADER_UNIFORM_FLOAT);
    }

    if (mat.uRimStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uRimStrengthLoc, &mat.params.fresnel, SHADER_UNIFORM_FLOAT);

    if (mat.uRefractionLoc >= 0)
        SetShaderValue(mat.shader, mat.uRefractionLoc, &mat.params.refraction, SHADER_UNIFORM_FLOAT);

    if (mat.uSparkleLoc >= 0)
        SetShaderValue(mat.shader, mat.uSparkleLoc, &mat.params.sparkle, SHADER_UNIFORM_FLOAT);

    if (mat.uCrackLoc >= 0)
        SetShaderValue(mat.shader, mat.uCrackLoc, &mat.params.crack, SHADER_UNIFORM_FLOAT);

    if (mat.uEmissionLoc >= 0)
        SetShaderValue(mat.shader, mat.uEmissionLoc, &mat.params.emission, SHADER_UNIFORM_FLOAT);

    if (mat.uThicknessLoc >= 0)
        SetShaderValue(mat.shader, mat.uThicknessLoc, &mat.params.thickness, SHADER_UNIFORM_FLOAT);

    if (mat.uDissolveLoc >= 0)
        SetShaderValue(mat.shader, mat.uDissolveLoc, &mat.params.dissolve, SHADER_UNIFORM_FLOAT);

    if (mat.uTexture1Loc >= 0 && mat.params.texture1.id != 0)
    {
        rlSetTexture(mat.params.texture1.id);
        SetShaderValueTexture(mat.shader, mat.uTexture1Loc, mat.params.texture1);
    }
}

void CrystalMaterial_End(void)
{
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    SkillManager_EndShader();
}

PlasmaMaterial PlasmaMaterial_Load(PlasmaMaterialParams params)
{
    PlasmaMaterial mat = {0};
    mat.shader = ResourceManager_LoadShader("core/shaders/plasma_shell.vs",
                                            "core/shaders/plasma_shell.fs");
    mat.params = params;

    mat.uBaseColorLoc = GetShaderLocation(mat.shader, "u_baseColor");
    mat.uWispColorLoc = GetShaderLocation(mat.shader, "u_wispColor");
    mat.uNoiseScaleLoc = GetShaderLocation(mat.shader, "u_noiseScale");
    mat.uNoiseSpeedLoc = GetShaderLocation(mat.shader, "u_noiseSpeed");
    mat.uFresnelPowerLoc = GetShaderLocation(mat.shader, "u_fresnelPower");
    mat.uRimStrengthLoc = GetShaderLocation(mat.shader, "u_rimStrength");
    mat.uEmissiveLoc = GetShaderLocation(mat.shader, "u_emissive");
    mat.uOpacityLoc = GetShaderLocation(mat.shader, "u_opacity");
    mat.uDisplaceAmpLoc = GetShaderLocation(mat.shader, "u_displaceAmp");
    mat.uTimeLoc = GetShaderLocation(mat.shader, "u_time");

    return mat;
}

void PlasmaMaterial_Begin(PlasmaMaterial mat)
{
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(mat.shader);

    float time = (float)GetTime();
    if (mat.uTimeLoc >= 0)
        SetShaderValue(mat.shader, mat.uTimeLoc, &time, SHADER_UNIFORM_FLOAT);

    if (mat.uBaseColorLoc >= 0)
    {
        Vector4 c = ColorNormalize(mat.params.baseColor);
        SetShaderValue(mat.shader, mat.uBaseColorLoc, &c, SHADER_UNIFORM_VEC4);
    }
    if (mat.uWispColorLoc >= 0)
    {
        Vector4 c = ColorNormalize(mat.params.wispColor);
        SetShaderValue(mat.shader, mat.uWispColorLoc, &c, SHADER_UNIFORM_VEC4);
    }
    if (mat.uNoiseScaleLoc >= 0)
        SetShaderValue(mat.shader, mat.uNoiseScaleLoc, &mat.params.noiseScale, SHADER_UNIFORM_FLOAT);
    if (mat.uNoiseSpeedLoc >= 0)
        SetShaderValue(mat.shader, mat.uNoiseSpeedLoc, &mat.params.noiseSpeed, SHADER_UNIFORM_FLOAT);
    if (mat.uFresnelPowerLoc >= 0)
        SetShaderValue(mat.shader, mat.uFresnelPowerLoc, &mat.params.fresnelPower, SHADER_UNIFORM_FLOAT);
    if (mat.uRimStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uRimStrengthLoc, &mat.params.rimStrength, SHADER_UNIFORM_FLOAT);
    if (mat.uEmissiveLoc >= 0)
        SetShaderValue(mat.shader, mat.uEmissiveLoc, &mat.params.emissive, SHADER_UNIFORM_FLOAT);
    if (mat.uOpacityLoc >= 0)
        SetShaderValue(mat.shader, mat.uOpacityLoc, &mat.params.opacity, SHADER_UNIFORM_FLOAT);
    if (mat.uDisplaceAmpLoc >= 0)
        SetShaderValue(mat.shader, mat.uDisplaceAmpLoc, &mat.params.displaceAmp, SHADER_UNIFORM_FLOAT);
}

void PlasmaMaterial_End(void)
{
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    SkillManager_EndShader();
}
