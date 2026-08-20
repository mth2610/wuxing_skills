#include "material_system.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * The parameter-table engine (M2, 20/08/2026)
 *
 * Replaces 6 hand-written FetchLocs() + 6 hand-written Begin() bodies, which
 * between them held 75 GetShaderLocation calls and 75 SetShaderValue calls
 * that differed only in which field of which struct they read.
 *
 * A material declares WHAT its uniforms are; this file owns HOW they are
 * fetched and pushed. Adding a uniform is one row in a table.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum
{
    VFXP_TIME = 0, /* GetTime(), no field                                      */
    VFXP_FLOAT,    /* float field, pushed as a + b*x clamped to [lo,hi]        */
    VFXP_COLOR,    /* Color field -> ColorNormalize -> vec4                    */
    VFXP_TEXTURE,  /* Texture2D field -> rlSetTexture + SetShaderValueTexture  */
    VFXP_TEXFLAG,  /* Texture2D field -> int (id != 0), the "has texture" bool */
    VFXP_CONST     /* literal float, no field                                  */
} VfxParamKind;

struct VfxParamDesc
{
    const char *name;    /* uniform name                                  */
    VfxParamKind kind;
    unsigned short offset; /* byte offset into the material's params struct */
    float a, b;          /* FLOAT: a + b*x   CONST: a                     */
    float lo, hi;        /* FLOAT: clamp range                            */
};

#define P_TIME(n)                 { n, VFXP_TIME,    0, 0.0f, 0.0f, 0.0f, 0.0f }
#define P_CONST(n, V)             { n, VFXP_CONST,   0, (V),  0.0f, 0.0f, 0.0f }
#define P_FLOAT(n, T, f)          { n, VFXP_FLOAT,   (unsigned short)offsetof(T, f), 0.0f, 1.0f, -1e30f, 1e30f }
#define P_AFFINE(n, T, f, A, B, LO, HI) \
                                  { n, VFXP_FLOAT,   (unsigned short)offsetof(T, f), (A), (B), (LO), (HI) }
#define P_COLOR(n, T, f)          { n, VFXP_COLOR,   (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }
#define P_TEX(n, T, f)            { n, VFXP_TEXTURE, (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }
#define P_TEXFLAG(n, T, f)        { n, VFXP_TEXFLAG, (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }

/* A table longer than the locs[] array would silently lose its tail uniforms,
 * which is exactly the invisible-failure mode this refactor exists to remove.
 * Fail the BUILD instead. */
#define VFX_STATIC_ASSERT(cond, tag) typedef char vfx_assert_##tag[(cond) ? 1 : -1]

/* ── The tables ──────────────────────────────────────────────────────────── */

/* u_customParam1/2 have no consumer in any shader and no caller sets them;
 * they stay in the table (and in the public params struct) only so the public
 * API does not change here. Their locs resolve to -1 and cost nothing. */
static const VfxParamDesc EFFECT_PARAMS[] = {
    P_TIME("u_time"),
    P_COLOR("u_baseColor", EffectMaterialParams, baseColor),
    P_FLOAT("u_translucency", EffectMaterialParams, translucency),
    P_FLOAT("u_rimStrength", EffectMaterialParams, rimStrength),
    P_FLOAT("u_fresnelPower", EffectMaterialParams, fresnelPower),
    P_FLOAT("u_emissiveIntensity", EffectMaterialParams, emissiveIntensity),
    P_FLOAT("u_distortionStrength", EffectMaterialParams, distortionStrength),
    /* ORDER MATTERS: the flag is pushed before the sampler, as the hand-written
     * code did — the fragment shader gates its texture1 fetch on it. */
    P_TEXFLAG("u_hasTexture1", EffectMaterialParams, texture1),
    P_TEX("texture1", EffectMaterialParams, texture1),
    P_FLOAT("u_customParam1", EffectMaterialParams, customParam1),
    P_FLOAT("u_customParam2", EffectMaterialParams, customParam2),
};
VFX_STATIC_ASSERT(sizeof(EFFECT_PARAMS) / sizeof(EFFECT_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, effect);

static const VfxParamDesc CRYSTAL_PARAMS[] = {
    P_TIME("u_time"),
    P_COLOR("u_baseColor", CrystalMaterialParams, baseColor),
    P_COLOR("u_edgeColor", CrystalMaterialParams, edgeColor),
    /* roughness 0..1 -> fresnelPower 8..1, floored at 1. Rougher = broader rim. */
    P_AFFINE("u_fresnelPower", CrystalMaterialParams, roughness, 8.0f, -7.0f, 1.0f, 1e30f),
    P_FLOAT("u_rimStrength", CrystalMaterialParams, fresnel),
    P_FLOAT("u_refraction", CrystalMaterialParams, refraction),
    P_FLOAT("u_sparkle", CrystalMaterialParams, sparkle),
    P_FLOAT("u_crack", CrystalMaterialParams, crack),
    P_FLOAT("u_emission", CrystalMaterialParams, emission),
    P_FLOAT("u_thickness", CrystalMaterialParams, thickness),
    P_FLOAT("u_dissolve", CrystalMaterialParams, dissolve),
    P_TEX("texture1", CrystalMaterialParams, texture1),
    /* Safe default: legacy immediate-mode crystal draws already baked their
     * grow progress on the CPU and must not be scaled down a second time.
     * CrystalMaterial_SetGrowProgress overrides it after Begin. */
    P_CONST("u_growProgress", 1.0f),
};
VFX_STATIC_ASSERT(sizeof(CRYSTAL_PARAMS) / sizeof(CRYSTAL_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, crystal);

static const VfxParamDesc PLASMA_PARAMS[] = {
    P_TIME("u_time"),
    P_COLOR("u_baseColor", PlasmaMaterialParams, baseColor),
    P_COLOR("u_wispColor", PlasmaMaterialParams, wispColor),
    P_FLOAT("u_noiseScale", PlasmaMaterialParams, noiseScale),
    P_FLOAT("u_noiseSpeed", PlasmaMaterialParams, noiseSpeed),
    P_FLOAT("u_fresnelPower", PlasmaMaterialParams, fresnelPower),
    P_FLOAT("u_rimStrength", PlasmaMaterialParams, rimStrength),
    P_FLOAT("u_emissive", PlasmaMaterialParams, emissive),
    P_FLOAT("u_opacity", PlasmaMaterialParams, opacity),
    P_FLOAT("u_displaceAmp", PlasmaMaterialParams, displaceAmp),
};
VFX_STATIC_ASSERT(sizeof(PLASMA_PARAMS) / sizeof(PLASMA_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, plasma);

static const VfxParamDesc AURA_PARAMS[] = {
    P_TIME("u_time"),
    P_COLOR("u_bodyColor", AuraShellMaterialParams, bodyColor),
    P_COLOR("u_glowColor", AuraShellMaterialParams, glowColor),
    P_FLOAT("u_opacity", AuraShellMaterialParams, opacity),
    P_FLOAT("u_fresnelPower", AuraShellMaterialParams, fresnelPower),
    P_FLOAT("u_rimStrength", AuraShellMaterialParams, rimStrength),
    P_FLOAT("u_scrollSpeed", AuraShellMaterialParams, scrollSpeed),
    P_FLOAT("u_noiseScale", AuraShellMaterialParams, noiseScale),
    P_FLOAT("u_heightScale", AuraShellMaterialParams, heightScale),
    P_FLOAT("u_scanFreq", AuraShellMaterialParams, scanFreq),
    P_FLOAT("u_scanSpeed", AuraShellMaterialParams, scanSpeed),
    P_FLOAT("u_scanStrength", AuraShellMaterialParams, scanStrength),
    P_FLOAT("u_displaceAmp", AuraShellMaterialParams, displaceAmp),
    P_FLOAT("u_topY", AuraShellMaterialParams, topY),
    P_FLOAT("u_heightFadeOff", AuraShellMaterialParams, heightFadeOff),
    P_FLOAT("u_coverFloor", AuraShellMaterialParams, coverFloor),
};
VFX_STATIC_ASSERT(sizeof(AURA_PARAMS) / sizeof(AURA_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, aura);

/* ── The engine ──────────────────────────────────────────────────────────── */

static void MatFetchLocs(Shader shader, const VfxParamDesc *layout, int count, int *locs)
{
    for (int i = 0; i < count; i++)
    {
        locs[i] = GetShaderLocation(shader, layout[i].name);
    }
}

static void MatApply(Shader shader, const VfxParamDesc *layout, int count,
                     const int *locs, const void *paramsBlob)
{
    const unsigned char *base = (const unsigned char *)paramsBlob;

    for (int i = 0; i < count; i++)
    {
        if (locs[i] < 0) continue; /* uniform absent or optimized out */

        switch (layout[i].kind)
        {
        case VFXP_TIME:
        {
            float t = (float)GetTime();
            SetShaderValue(shader, locs[i], &t, SHADER_UNIFORM_FLOAT);
            break;
        }
        case VFXP_CONST:
        {
            float v = layout[i].a;
            SetShaderValue(shader, locs[i], &v, SHADER_UNIFORM_FLOAT);
            break;
        }
        case VFXP_FLOAT:
        {
            float x;
            memcpy(&x, base + layout[i].offset, sizeof(x));
            float v = layout[i].a + layout[i].b * x;
            if (v < layout[i].lo) v = layout[i].lo;
            if (v > layout[i].hi) v = layout[i].hi;
            SetShaderValue(shader, locs[i], &v, SHADER_UNIFORM_FLOAT);
            break;
        }
        case VFXP_COLOR:
        {
            Color c;
            memcpy(&c, base + layout[i].offset, sizeof(c));
            Vector4 v = ColorNormalize(c);
            SetShaderValue(shader, locs[i], &v, SHADER_UNIFORM_VEC4);
            break;
        }
        case VFXP_TEXFLAG:
        {
            Texture2D t;
            memcpy(&t, base + layout[i].offset, sizeof(t));
            int has = (t.id != 0);
            SetShaderValue(shader, locs[i], &has, SHADER_UNIFORM_INT);
            break;
        }
        case VFXP_TEXTURE:
        {
            Texture2D t;
            memcpy(&t, base + layout[i].offset, sizeof(t));
            if (t.id != 0)
            {
                rlSetTexture(t.id);
                SetShaderValueTexture(shader, locs[i], t);
            }
            break;
        }
        }
    }
}

/* Look a uniform up by name within a material's own table. Used by the few
 * setters that push a value outside Begin(); a strcmp over ~13 short names is
 * nothing next to the draw call that follows, and unlike a hard-coded index it
 * cannot drift when a row is inserted. */
static int MatLocByName(const VfxParamDesc *layout, int count, const int *locs, const char *name)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(layout[i].name, name) == 0) return locs[i];
    }
    return -1;
}

/* Every material opens and closes the same way. */
static void MatBeginCommon(Shader shader)
{
    /* Raylib batching hazard: flush the active batch before changing shader or
     * texture state, or queued geometry is drawn with the NEW state. */
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(shader);
}

static void MatEndCommon(void)
{
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    SkillManager_EndShader();
}

/* The INSTANCED permutation. One source .vs, two programs — see
 * core/shaders/common/vs_instanced_header.glsl for why this must be a
 * compile-time define and not a runtime branch. */
#define VFX_DEFINE_INSTANCED "#define INSTANCED 1\n"

/* ═══════════════════════════════════════════════════════════════════════════
 * EffectMaterial
 * ═══════════════════════════════════════════════════════════════════════════ */

void MaterialSystem_Init(void)
{
    // Mồi trước shader dùng chung
    ResourceManager_LoadShader("core/shaders/effect_material.vs", "core/shaders/effect_material.fs");
}

void MaterialSystem_Unload(void)
{
    // Để trống, resource_manager tự lo dọn dẹp
}

void Material_Get(EffectMaterial *outMat, MaterialPreset preset)
{
    if (!outMat) return;
    EffectMaterialParams p = {0};

    switch (preset)
    {
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

    Material_LoadCustom(outMat, &p);
    outMat->preset = preset;
}

/* One place that binds a shader to the Effect table, so the four Effect
 * loaders below cannot disagree about it. */
static void EffectMaterial_Bind(EffectMaterial *outMat, Shader shader,
                                const EffectMaterialParams *params)
{
    *outMat = (EffectMaterial){0};
    outMat->preset = MAT_CUSTOM;
    outMat->shader = shader;
    outMat->layout = EFFECT_PARAMS;
    outMat->layoutCount = (int)(sizeof(EFFECT_PARAMS) / sizeof(EFFECT_PARAMS[0]));
    MatFetchLocs(outMat->shader, outMat->layout, outMat->layoutCount, outMat->locs);
    if (params) outMat->params = *params;
}

void Material_LoadCustom(EffectMaterial *outMat, const EffectMaterialParams *params)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat,
                        ResourceManager_LoadShader("core/shaders/effect_material.vs",
                                                   "core/shaders/effect_material.fs"),
                        params);
}

void Material_LoadCustomShader(EffectMaterial *outMat, const EffectMaterialParams *params,
                               const char *vsPath, const char *fsPath)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat, ResourceManager_LoadShader(vsPath, fsPath), params);
}

void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val)
{
    int loc = GetShaderLocation(mat->shader, uniformName);
    if (loc >= 0)
    {
        SetShaderValue(mat->shader, loc, &val, SHADER_UNIFORM_FLOAT);
    }
}

void Material_Begin(EffectMaterial mat)
{
    MatBeginCommon(mat.shader);
    MatApply(mat.shader, mat.layout, mat.layoutCount, mat.locs, &mat.params);
}

void Material_End(void)
{
    MatEndCommon();
}

void EffectMaterialInstanced_Load(EffectMaterialInstanced *outMat, const EffectMaterialParams *params)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat,
                        ResourceManager_LoadShaderVariant("core/shaders/effect_material.vs",
                                                          "core/shaders/effect_material.fs",
                                                          VFX_DEFINE_INSTANCED),
                        params);
}

void EffectMaterialInstanced_Begin(EffectMaterialInstanced mat)
{
    Material_Begin(mat);
}

void EffectMaterialInstanced_End(void)
{
    MatEndCommon();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CrystalMaterial
 * ═══════════════════════════════════════════════════════════════════════════ */

static void CrystalMaterial_Bind(CrystalMaterial *outMat, Shader shader,
                                 const CrystalMaterialParams *params)
{
    *outMat = (CrystalMaterial){0};
    outMat->shader = shader;
    outMat->layout = CRYSTAL_PARAMS;
    outMat->layoutCount = (int)(sizeof(CRYSTAL_PARAMS) / sizeof(CRYSTAL_PARAMS[0]));
    if (params) outMat->params = *params;

    if (outMat->params.texture1.id == 0)
    {
        outMat->params.texture1 = ResourceManager_LoadTexture("assets/textures/tex_crystal.png");
    }

    MatFetchLocs(outMat->shader, outMat->layout, outMat->layoutCount, outMat->locs);
}

void CrystalMaterial_Load(CrystalMaterial *outMat, const CrystalMaterialParams *params)
{
    if (!outMat) return;
    CrystalMaterial_Bind(outMat,
                         ResourceManager_LoadShader("core/shaders/crystal.vs",
                                                    "core/shaders/crystal.fs"),
                         params);
}

void CrystalMaterial_Begin(CrystalMaterial mat)
{
    MatBeginCommon(mat.shader);
    MatApply(mat.shader, mat.layout, mat.layoutCount, mat.locs, &mat.params);
}

void CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress)
{
    int loc = MatLocByName(mat.layout, mat.layoutCount, mat.locs, "u_growProgress");
    if (loc < 0) return;
    SetShaderValue(mat.shader, loc, &progress, SHADER_UNIFORM_FLOAT);
}

void CrystalMaterial_End(void)
{
    MatEndCommon();
}

void CrystalMaterialInstanced_Load(CrystalMaterialInstanced *outMat, const CrystalMaterialParams *params)
{
    if (!outMat) return;
    CrystalMaterial_Bind(outMat,
                         ResourceManager_LoadShaderVariant("core/shaders/crystal.vs",
                                                           "core/shaders/crystal.fs",
                                                           VFX_DEFINE_INSTANCED),
                         params);
}

void CrystalMaterialInstanced_Begin(CrystalMaterialInstanced mat)
{
    CrystalMaterial_Begin(mat);
}

void CrystalMaterialInstanced_SetGrowProgress(CrystalMaterialInstanced mat, float progress)
{
    CrystalMaterial_SetGrowProgress(mat, progress);
}

void CrystalMaterialInstanced_End(void)
{
    MatEndCommon();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PlasmaMaterial
 * ═══════════════════════════════════════════════════════════════════════════ */

void PlasmaMaterial_Load(PlasmaMaterial *outMat, const PlasmaMaterialParams *params)
{
    if (!outMat) return;
    *outMat = (PlasmaMaterial){0};
    outMat->shader = ResourceManager_LoadShader("core/shaders/plasma_shell.vs",
                                                "core/shaders/plasma_shell.fs");
    outMat->layout = PLASMA_PARAMS;
    outMat->layoutCount = (int)(sizeof(PLASMA_PARAMS) / sizeof(PLASMA_PARAMS[0]));
    if (params) outMat->params = *params;
    MatFetchLocs(outMat->shader, outMat->layout, outMat->layoutCount, outMat->locs);
}

void PlasmaMaterial_Begin(PlasmaMaterial mat)
{
    MatBeginCommon(mat.shader);
    MatApply(mat.shader, mat.layout, mat.layoutCount, mat.locs, &mat.params);
}

void PlasmaMaterial_End(void)
{
    MatEndCommon();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AuraShellMaterial
 * ═══════════════════════════════════════════════════════════════════════════ */

void AuraShellMaterial_Load(AuraShellMaterial *outMat, const AuraShellMaterialParams *params)
{
    if (!outMat) return;
    *outMat = (AuraShellMaterial){0};
    outMat->shader = ResourceManager_LoadShader("core/shaders/aura_shell.vs",
                                                "core/shaders/aura_shell.fs");
    outMat->layout = AURA_PARAMS;
    outMat->layoutCount = (int)(sizeof(AURA_PARAMS) / sizeof(AURA_PARAMS[0]));
    if (params) outMat->params = *params;
    MatFetchLocs(outMat->shader, outMat->layout, outMat->layoutCount, outMat->locs);
}

void AuraShellMaterial_Begin(AuraShellMaterial mat)
{
    MatBeginCommon(mat.shader);
    MatApply(mat.shader, mat.layout, mat.layoutCount, mat.locs, &mat.params);
}

void AuraShellMaterial_End(void)
{
    MatEndCommon();
}
