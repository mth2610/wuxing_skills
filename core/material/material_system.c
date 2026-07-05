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
