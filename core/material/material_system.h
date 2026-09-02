#ifndef MATERIAL_SYSTEM_H
#define MATERIAL_SYSTEM_H

#include "raylib.h"
#include "core/vfx_render.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Parameter table (M2, 20/08/2026)
 *
 * Every material used to carry its own hand-written list of `int u*Loc`
 * fields — 66 of them across 6 structs — plus a matching FetchLocs() and a
 * matching Begin() that repeated `if (loc >= 0) SetShaderValue(...)` once per
 * uniform. Adding one uniform meant editing three places, and forgetting the
 * third failed silently: an unset uniform reads as 0 and still renders.
 *
 * Now each material names a static table of VfxParamDesc (uniform name, kind,
 * offset into its own params struct). One fetch loop and one apply loop serve
 * all of them, and `locs[i]` corresponds to `layout[i]`. Adding a uniform is
 * one table row.
 *
 * VfxParamDesc is opaque here on purpose: the tables live in
 * material_system.c and nothing outside needs their shape.
 * ───────────────────────────────────────────────────────────────────────── */
#define VFX_MAT_MAX_PARAMS 20

typedef struct VfxParamDesc VfxParamDesc;

typedef enum
{
    MAT_FIRE,
    MAT_ICE,
    MAT_WATER,
    MAT_PORTAL,
    MAT_ROCK,
    MAT_METAL,
    MAT_GLASS,
    MAT_CUSTOM
} MaterialPreset;

// Tương thích ngược với hệ thống Material cũ
#define MATERIAL_FIRE MAT_FIRE
#define MATERIAL_ICE MAT_ICE
#define MATERIAL_WATER MAT_WATER
#define MATERIAL_PORTAL MAT_PORTAL
#define MATERIAL_CUSTOM MAT_CUSTOM
#define Material_Load Material_Get

typedef struct
{
    Color baseColor;          // primary tint; also drives rim glow + dissolve edge glow color
    float rimStrength;        // 0..~2, rim/edge glow brightness (Fresnel-weighted)
    float fresnelPower;       // 1..8, rim sharpness (higher = thinner edge)
    float emissiveIntensity;  // 0..~3, self-illumination boost added to base color
    float distortionStrength; // 0..1, vertex wobble amount
    float translucency;       // 0..1: 0 = opaque (alpha = baseColor.a), 1 = glass/tube-style
                              // fresnel-driven alpha (center see-through, edges more solid,
                              // same formula as tube.fs). Draw call must use BLEND_ALPHA
                              // (BeginBlendMode/EndBlendMode) for this to actually blend.
    Texture2D texture1;       // optional secondary detail/mask texture; id==0 = unused
    float customParam1;       // custom generic float param passed to shader
    float customParam2;       // second custom generic float param passed to shader
} EffectMaterialParams;

/* Explicit output semantics for the opt-in VFX path. Legacy loaders never read
 * this struct and retain their caller-managed alpha BODY behavior exactly. */
typedef struct
{
    VFXSurfaceMode surface;
    float bodyOpacity;       /* Coverage multiplier; clamped to 0..1 in GLSL. */
    Color emissionColor;     /* Base hue normalized to 0..1; HDR gain is separate. */
    float emissionIntensity; /* HDR gain; may exceed 1.0. */
    float coreMask;          /* Uniform emission mask, 0..1. */
} EffectMaterialVFXOutput;

typedef struct
{
    Shader shader;
    MaterialPreset preset;
    const VfxParamDesc *layout; // static table in material_system.c, not owned
    int layoutCount;
    int locs[VFX_MAT_MAX_PARAMS]; // locs[i] belongs to layout[i]
    EffectMaterialParams params;
    /* Append-only: old aggregate/zero initialization remains valid. */
    VFXSurfaceMode surface;
    int vfxOutputEnabled;
    EffectMaterialVFXOutput vfxOutput;
    int vfxBodyOpacityLoc;
    int vfxEmissionColorLoc;
    int vfxEmissionIntensityLoc;
    int vfxCoreMaskLoc;
} EffectMaterial;

// Khởi tạo hệ thống chất liệu (Load sẵn Shader/Texture cho các preset)
void MaterialSystem_Init(void);

// Giải phóng hệ thống chất liệu
void MaterialSystem_Unload(void);

// Lấy chất liệu theo preset chuẩn
void Material_Get(EffectMaterial *outMat, MaterialPreset preset);

// Load chất liệu tùy biến
void Material_LoadCustom(EffectMaterial *outMat, const EffectMaterialParams *params);

// Load chất liệu tùy biến với shader file riêng (chỉ dùng khi cần thay thế shader gốc)
void Material_LoadCustomShader(EffectMaterial *outMat, const EffectMaterialParams *params, const char* vsPath, const char* fsPath);

/* Surface-aware opt-in loaders. `surface` selects the OUTPUT_* shader
 * permutation. A custom fragment shader must include vfx_composite.glsl and
 * finish through VFX_ResolveOutput(). */
void Material_LoadCustomVFX(EffectMaterial *outMat,
                            const EffectMaterialParams *params,
                            const EffectMaterialVFXOutput *output);
void Material_LoadCustomShaderVFX(EffectMaterial *outMat,
                                  const EffectMaterialParams *params,
                                  const char *vsPath, const char *fsPath,
                                  const EffectMaterialVFXOutput *output);

// Gán uniform float cho shader
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);

// Bắt đầu dùng chất liệu (đảm bảo an toàn batching của Raylib)
void Material_Begin(EffectMaterial mat);

// Kết thúc dùng chất liệu
void Material_End(void);

/* Owns target + blend + depth + shader as one scope. Returns false for legacy
 * materials and mismatched pairs: ADDITIVE requires EMISSION; ALPHA and
 * PREMULTIPLIED require BODY. No render state changes on rejection. */
bool Material_BeginVFX(EffectMaterial mat, VFXRenderPass pass, bool depthWrite,
                       VFXRenderScope *outScope);
void Material_EndVFX(VFXRenderScope *scope);

/* Biến thể GPU-instancing của EffectMaterial — dùng shader program RIÊNG
 * (core/shaders/effect_material_instanced.vs, cùng effect_material.fs) nên
 * cần cache uniform location riêng, giống lý do CrystalMaterialInstanced
 * tách khỏi CrystalMaterial (location của cùng 1 tên uniform có thể khác
 * nhau giữa 2 program đã link khác nhau). Dùng với DrawMeshInstanced để vẽ
 * N mesh giống nhau (rock, shard...) bằng ĐÚNG 1 draw call thay vì N lần
 * DrawMesh. KHÔNG còn consumer nào kể từ khi F0 xoá VFX_ComposeFloatingStones
 * — đường instanced của EffectMaterial hiện chỉ được canh bởi autotest
 * `shader_permutation`. Chỉ dùng cho hiệu ứng backed bởi EffectMaterial
 * (Material_Get/Material_LoadCustom) — hiệu ứng dùng CrystalMaterial thì
 * dùng CrystalMaterialInstanced ở trên thay vì cái này. */
/* Same struct, same layout table, same shader source — the ONLY difference
 * is the INSTANCED permutation passed to the shader loader. It was a separate
 * struct (and a separate hand-copied .vs) until 20/08/2026; the copy had
 * already drifted from its original. */
typedef EffectMaterial EffectMaterialInstanced;

void EffectMaterialInstanced_Load(EffectMaterialInstanced *outMat, const EffectMaterialParams *params);
void EffectMaterialInstanced_Begin(EffectMaterialInstanced mat);
void EffectMaterialInstanced_End(void);

/* ============================================================================
 * CRYSTAL MATERIAL SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Một shader duy nhất để vẽ mọi loại tinh thể (Băng, Kim cương, Thạch anh, Ngọc)
 * hỗ trợ các tham số: Fresnel, fake refraction, thickness, sparkles, nứt...
 * ==========================================================================*/

typedef struct
{
    Color baseColor;
    Color edgeColor;
    float roughness;    // mapped to u_fresnelPower = clamp(8.0 - 7.0*roughness, 1.0, +inf)
                        // (rougher = LOWER fresnel power = broader rim)
    float fresnel;      // mapped to u_rimStrength
    float refraction;   // mapped to u_refraction (distortion)
    float sparkle;      // mapped to u_sparkle (specular sparkle)
    float crack;        // mapped to u_crack (internal noise cracks)
    float emission;     // mapped to u_emission (glow boost)
    float thickness;    // mapped to u_thickness (light absorption)
    float dissolve;     // mapped to u_dissolve (dissolve progress)
    Texture2D texture1; // detail caustics texture for fake refraction
} CrystalMaterialParams;

typedef struct
{
    Shader shader;
    CrystalMaterialParams params;
    const VfxParamDesc *layout;
    int layoutCount;
    int locs[VFX_MAT_MAX_PARAMS];
} CrystalMaterial;

void CrystalMaterial_Load(CrystalMaterial *outMat, const CrystalMaterialParams *params);
void CrystalMaterial_Begin(CrystalMaterial mat);
void CrystalMaterial_End(void);

/* Set u_growProgress (0..1) cho hiệu ứng "mọc lên" của Mesh GPU-resident từ
 * ProceduralMesh_BuildCrystalClusterMesh — gọi giữa Begin/End, trước
 * DrawMesh/ProceduralMesh_DrawBakedCrystalCluster. CrystalMaterial_Begin luôn
 * reset về 1.0 nên chỉ cần gọi hàm này khi thực sự đang animate mọc lên. */
void CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress);

/* Biến thể GPU-instancing của CrystalMaterial — dùng shader program RIÊNG
 * (core/shaders/crystal_instanced.vs, cùng crystal.fs) nên cần cache uniform
 * location riêng (location của cùng 1 tên uniform có thể khác nhau giữa 2
 * program đã link khác nhau) — không thể tái dùng struct CrystalMaterial ở
 * trên. Dùng với DrawMeshInstanced để vẽ N mesh giống nhau bằng ĐÚNG 1 draw
 * call thay vì N lần DrawMesh — xem VFX_DrawIceCrystalBurst (vc_water.inl)
 * và CORE_ISSUES.md Item 40.
 * ĐÁNH ĐỔI: u_growProgress dùng chung cho cả batch (không per-instance được
 * — instancing chỉ có 1 bộ uniform/draw call), nên không hỗ trợ "mọc so le"
 * như bản CrystalMaterial thường. */
/* See EffectMaterialInstanced: same struct, INSTANCED permutation only. */
typedef CrystalMaterial CrystalMaterialInstanced;

void CrystalMaterialInstanced_Load(CrystalMaterialInstanced *outMat, const CrystalMaterialParams *params);
void CrystalMaterialInstanced_Begin(CrystalMaterialInstanced mat);
void CrystalMaterialInstanced_SetGrowProgress(CrystalMaterialInstanced mat, float progress);
void CrystalMaterialInstanced_End(void);

/* ============================================================================
 * PLASMA MATERIAL SYSTEM
 * --------------------------------------------------------------------------
 * Wispy energy membrane (plasma_shell.vs/.fs): fresnel × animated fbm alpha,
 * fully transparent at the center — the shell look EffectMaterial's
 * translucency cannot do (it has a 0.3 alpha floor face-on). Draw spheres
 * with alpha blend for translucent shields; the caller owns the blend mode.
 * ==========================================================================*/

typedef struct
{
    Color baseColor;    // deep body tint of the membrane (alpha = master alpha)
    Color wispColor;    // bright filament-crest tint
    float noiseScale;   // wisp frequency over the sphere (try 2.5-4.0)
    float noiseSpeed;   // wisp scroll speed (try 0.3-0.8)
    float fresnelPower; // rim sharpness 1..8 (higher = emptier center)
    float rimStrength;  // extra rim brightness 0..~2
    float emissive;     // HDR gain 0..8, applied only to narrow filament crests
    float opacity;      // master alpha multiplier 0..1
    float displaceAmp;  // world-units vertex undulation amplitude
} PlasmaMaterialParams;

typedef struct
{
    Shader shader;
    PlasmaMaterialParams params;
    const VfxParamDesc *layout;
    int layoutCount;
    int locs[VFX_MAT_MAX_PARAMS];
} PlasmaMaterial;

void PlasmaMaterial_Load(PlasmaMaterial *outMat, const PlasmaMaterialParams *params);
void PlasmaMaterial_Begin(PlasmaMaterial mat);
void PlasmaMaterial_End(void);
#endif // MATERIAL_SYSTEM_H
