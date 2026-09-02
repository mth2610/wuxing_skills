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

/* There is NO time kind. u_time is auto-bound by SkillManager_BeginShader and,
 * as core/shaders/common/fs_header.glsl puts it, ONLY by it — from
 * g_skillManagerTime, which accumulates the pinned delta. This table used to
 * push GetTime() on top of that, i.e. overwrite a reproducible clock with the
 * wall clock, one call after MatBeginCommon had set it correctly. Measured
 * cost: ENERGY ORB's warm body%% swung 3.97 to 5.39 and its cool darken%% 46.1
 * to 57.7 between runs of the SAME binary, because the wisp noise phase
 * depended on how long the process took to reach the captured frame. */
typedef enum
{
    VFXP_FLOAT = 0,    /* float field, pushed as a + b*x clamped to [lo,hi]        */
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

#define P_CONST(n, V)             { n, VFXP_CONST,   0, (V),  0.0f, 0.0f, 0.0f }
#define P_FLOAT(n, T, f)          { n, VFXP_FLOAT,   (unsigned short)offsetof(T, f), 0.0f, 1.0f, -1e30f, 1e30f }
#define P_AFFINE(n, T, f, A, B, LO, HI) \
                                  { n, VFXP_FLOAT,   (unsigned short)offsetof(T, f), (A), (B), (LO), (HI) }
#define P_COLOR(n, T, f)          { n, VFXP_COLOR,   (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }
#define P_TEX(n, T, f)            { n, VFXP_TEXTURE, (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }
#define P_TEXFLAG(n, T, f)        { n, VFXP_TEXFLAG, (unsigned short)offsetof(T, f), 0.0f, 0.0f, 0.0f, 0.0f }

/* A texture a material falls back to when the caller supplies none. Generated
 * from a `.mat`'s `default:`, so no asset path is spelled in C. */
typedef struct
{
    unsigned short offset; /* byte offset of the Texture2D field */
    const char    *path;
} VfxTextureDefault;

/* Fill in every default whose field the caller left empty. */
static void MatApplyTextureDefaults(const VfxTextureDefault *defaults, int count,
                                    void *paramsBlob)
{
    unsigned char *base = (unsigned char *)paramsBlob;
    for (int i = 0; i < count; i++)
    {
        Texture2D existing;
        memcpy(&existing, base + defaults[i].offset, sizeof(existing));
        if (existing.id != 0) continue;
        Texture2D loaded = ResourceManager_LoadTexture(defaults[i].path);
        memcpy(base + defaults[i].offset, &loaded, sizeof(loaded));
    }
}

/* A table longer than the locs[] array would silently lose its tail uniforms,
 * which is exactly the invisible-failure mode this refactor exists to remove.
 * Fail the BUILD instead. */
#define VFX_STATIC_ASSERT(cond, tag) typedef char vfx_assert_##tag[(cond) ? 1 : -1]

/* ── The tables ──────────────────────────────────────────────────────────── */

/* EFFECT_PARAMS and CRYSTAL_PARAMS are GENERATED from
 * the .mat files under core/shading/materials, which are also where their .fs
 * files come from. (Spelled out rather than globbed: a "/" followed by a "*"
 * inside a block comment is a nested comment opener, and -Wcomment says so.)
 * That is the point: a material's uniform list used to exist twice — once as
 * `uniform` lines in the shader, once as a table here — and the two drifted with
 * nothing to compare them. Included after the P_* macros and the params structs
 * it references, and before the size assertions below.
 *
 * PLASMA_PARAMS is still hand-written, on purpose. PlasmaMaterial has no
 * consumer: converting it would be building format support for something
 * nothing calls. It is not deleted either — unlike the shaders removed this
 * session, which each had a live replacement, plasma is a capability with none
 * (a shell that is empty face-on, which EffectMaterial's 0.3 alpha floor cannot
 * produce). It converts the day it gains a caller. */
#include "materials.generated.inl"

VFX_STATIC_ASSERT(sizeof(EFFECT_PARAMS) / sizeof(EFFECT_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, effect);

VFX_STATIC_ASSERT(sizeof(CRYSTAL_PARAMS) / sizeof(CRYSTAL_PARAMS[0]) <= VFX_MAT_MAX_PARAMS, crystal);

static const VfxParamDesc PLASMA_PARAMS[] = {
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

static const char *EffectMaterial_OutputDefines(
    VFXSurfaceMode surface, EffectMaterialGeometryMode geometryMode)
{
    const bool immediate = geometryMode == EFFECT_MATERIAL_GEOMETRY_IMMEDIATE;
    switch (surface)
    {
    case VFX_SURFACE_ADDITIVE:
        return immediate
            ? "#define OUTPUT_EMISSION 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n#define EFFECT_MATERIAL_IMMEDIATE 1\n"
            : "#define OUTPUT_EMISSION 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n";
    case VFX_SURFACE_PREMULTIPLIED:
        return immediate
            ? "#define OUTPUT_PREMULTIPLIED 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n#define EFFECT_MATERIAL_IMMEDIATE 1\n"
            : "#define OUTPUT_PREMULTIPLIED 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n";
    case VFX_SURFACE_ALPHA:
    default:
        return immediate
            ? "#define OUTPUT_BODY 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n#define EFFECT_MATERIAL_IMMEDIATE 1\n"
            : "#define OUTPUT_BODY 1\n#define VFX_TONEMAP_SAFE_EMISSION 1\n";
    }
}

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

    /* The preset table is GENERATED from effect_material.mat. It used to be a
     * ~70-line switch here, which is the wrong shape for what it is: seven sets
     * of authored numbers, differing only in their values. A preset outside the
     * table (MAT_CUSTOM, or a value from a future enum) yields zeroed params,
     * exactly as the switch's `default: break;` did. */
    EffectMaterialParams p = {0};
    const int presetCount = (int)(sizeof(EFFECT_PARAMS_PRESETS) /
                                  sizeof(EFFECT_PARAMS_PRESETS[0]));
    if ((int)preset >= 0 && (int)preset < presetCount)
    {
        p = EFFECT_PARAMS_PRESETS[preset];

        /* A texture is loaded, not initialised, so it cannot sit in the table
         * above. Its path can, which is the point: no asset path in C. */
        const int texCount = (int)(sizeof(EFFECT_PARAMS_PRESET_TEXTURES) /
                                   sizeof(EFFECT_PARAMS_PRESET_TEXTURES[0]));
        if ((int)preset < texCount && EFFECT_PARAMS_PRESET_TEXTURES[preset])
        {
            p.texture1 = ResourceManager_LoadTexture(EFFECT_PARAMS_PRESET_TEXTURES[preset]);
        }
    }

    Material_LoadCustom(outMat, &p);
    outMat->preset = preset;
}

/* One place that binds a shader to the Effect table, so the four Effect
 * loaders below cannot disagree about it. */
static void EffectMaterial_Bind(EffectMaterial *outMat, Shader shader,
                                const EffectMaterialParams *params,
                                const EffectMaterialVFXOutput *output)
{
    *outMat = (EffectMaterial){0};
    outMat->preset = MAT_CUSTOM;
    outMat->shader = shader;
    outMat->layout = EFFECT_PARAMS;
    outMat->layoutCount = (int)(sizeof(EFFECT_PARAMS) / sizeof(EFFECT_PARAMS[0]));
    MatFetchLocs(outMat->shader, outMat->layout, outMat->layoutCount, outMat->locs);
    if (params) outMat->params = *params;
    outMat->surface = EFFECT_PARAMS_OUTPUT_SURFACE;
    if (output)
    {
        VFXSurfaceMode surface = output->surface;
        if (surface < VFX_SURFACE_ALPHA || surface > VFX_SURFACE_PREMULTIPLIED)
            surface = VFX_SURFACE_ALPHA;
        outMat->vfxOutputEnabled = 1;
        outMat->surface = surface;
        outMat->vfxOutput = *output;
        outMat->vfxOutput.surface = surface;
        outMat->vfxBodyOpacityLoc = GetShaderLocation(shader, "u_vfxBodyOpacity");
        outMat->vfxEmissionColorLoc = GetShaderLocation(shader, "u_vfxEmissionColor");
        outMat->vfxEmissionIntensityLoc = GetShaderLocation(shader, "u_vfxEmissionIntensity");
        outMat->vfxCoreMaskLoc = GetShaderLocation(shader, "u_vfxCoreMask");
    }
}

void Material_LoadCustom(EffectMaterial *outMat, const EffectMaterialParams *params)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat,
                        ResourceManager_LoadShader("core/shaders/effect_material.vs",
                                                   "core/shaders/effect_material.fs"),
                        params, NULL);
}

void Material_LoadCustomShader(EffectMaterial *outMat, const EffectMaterialParams *params,
                               const char *vsPath, const char *fsPath)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat, ResourceManager_LoadShader(vsPath, fsPath),
                        params, NULL);
}

void Material_LoadCustomVFX(EffectMaterial *outMat,
                            const EffectMaterialParams *params,
                            const EffectMaterialVFXOutput *output)
{
    VFXSurfaceMode surface;
    if (!outMat || !output) return;
    surface = output->surface;
    if (surface < VFX_SURFACE_ALPHA || surface > VFX_SURFACE_PREMULTIPLIED)
        surface = VFX_SURFACE_ALPHA;
    EffectMaterial_Bind(
        outMat,
        ResourceManager_LoadShaderVariant("core/shaders/effect_material.vs",
                                          "core/shaders/effect_material.fs",
                                          EffectMaterial_OutputDefines(surface,
                                              output->geometryMode)),
        params, output);
}

void Material_LoadCustomShaderVFX(EffectMaterial *outMat,
                                  const EffectMaterialParams *params,
                                  const char *vsPath, const char *fsPath,
                                  const EffectMaterialVFXOutput *output)
{
    VFXSurfaceMode surface;
    if (!outMat || !output) return;
    surface = output->surface;
    if (surface < VFX_SURFACE_ALPHA || surface > VFX_SURFACE_PREMULTIPLIED)
        surface = VFX_SURFACE_ALPHA;
    EffectMaterial_Bind(
        outMat,
        ResourceManager_LoadShaderVariant(vsPath, fsPath,
                                          EffectMaterial_OutputDefines(surface,
                                              output->geometryMode)),
        params, output);
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
    if (mat.vfxOutputEnabled)
    {
        Vector4 emission = ColorNormalize(mat.vfxOutput.emissionColor);
        if (mat.vfxBodyOpacityLoc >= 0)
            SetShaderValue(mat.shader, mat.vfxBodyOpacityLoc,
                           &mat.vfxOutput.bodyOpacity, SHADER_UNIFORM_FLOAT);
        if (mat.vfxEmissionColorLoc >= 0)
            SetShaderValue(mat.shader, mat.vfxEmissionColorLoc,
                           &emission, SHADER_UNIFORM_VEC4);
        if (mat.vfxEmissionIntensityLoc >= 0)
            SetShaderValue(mat.shader, mat.vfxEmissionIntensityLoc,
                           &mat.vfxOutput.emissionIntensity, SHADER_UNIFORM_FLOAT);
        if (mat.vfxCoreMaskLoc >= 0)
            SetShaderValue(mat.shader, mat.vfxCoreMaskLoc,
                           &mat.vfxOutput.coreMask, SHADER_UNIFORM_FLOAT);
    }
}

void Material_End(void)
{
    MatEndCommon();
}

bool Material_BeginVFX(EffectMaterial mat, VFXRenderPass pass, bool depthWrite,
                       VFXRenderScope *outScope)
{
    VFXRenderPass expectedPass;
    if (!mat.vfxOutputEnabled || outScope == NULL) return false;
    expectedPass = mat.surface == VFX_SURFACE_ADDITIVE
                       ? VFX_RENDER_PASS_EMISSION : VFX_RENDER_PASS_BODY;
    if (pass != expectedPass) return false;
    *outScope = VFXRender_BeginDraw(pass, mat.surface, depthWrite);
    Material_Begin(mat);
    return true;
}

void Material_EndVFX(VFXRenderScope *scope)
{
    if (scope == NULL || !scope->active) return;
    Material_End();
    VFXRender_EndDraw(scope);
}

void EffectMaterialInstanced_Load(EffectMaterialInstanced *outMat, const EffectMaterialParams *params)
{
    if (!outMat) return;
    EffectMaterial_Bind(outMat,
                        ResourceManager_LoadShaderVariant("core/shaders/effect_material.vs",
                                                          "core/shaders/effect_material.fs",
                                                          VFX_DEFINE_INSTANCED),
                        params, NULL);
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

    MatApplyTextureDefaults(CRYSTAL_PARAMS_DEFAULTS,
                            (int)(sizeof(CRYSTAL_PARAMS_DEFAULTS) /
                                  sizeof(CRYSTAL_PARAMS_DEFAULTS[0])),
                            &outMat->params);

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
