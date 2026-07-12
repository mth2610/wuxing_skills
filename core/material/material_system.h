#ifndef MATERIAL_SYSTEM_H
#define MATERIAL_SYSTEM_H

#include "raylib.h"

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

typedef struct
{
    Shader shader;
    MaterialPreset preset;
    int uTimeLoc;
    int uDissolveLoc;
    int uBaseColorLoc;
    int uTranslucencyLoc;
    int uRimStrengthLoc;
    int uFresnelPowerLoc;
    int uEmissiveIntensityLoc;
    int uDistortionStrengthLoc;
    int uHasTexture1Loc;
    int uTexture1Loc;
    int uCustomParam1Loc;
    int uCustomParam2Loc;
    EffectMaterialParams params;
} EffectMaterial;

// Khởi tạo hệ thống chất liệu (Load sẵn Shader/Texture cho các preset)
void MaterialSystem_Init(void);

// Giải phóng hệ thống chất liệu
void MaterialSystem_Unload(void);

// Lấy chất liệu theo preset chuẩn
EffectMaterial Material_Get(MaterialPreset preset);

// Load chất liệu tùy biến
EffectMaterial Material_LoadCustom(EffectMaterialParams params);

// Load chất liệu tùy biến với shader file riêng (chỉ dùng khi cần thay thế shader gốc)
EffectMaterial Material_LoadCustomShader(EffectMaterialParams params, const char* vsPath, const char* fsPath);

// Gán uniform float cho shader
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);

// Bắt đầu dùng chất liệu (đảm bảo an toàn batching của Raylib)
void Material_Begin(EffectMaterial mat);

// Kết thúc dùng chất liệu
void Material_End(void);

/* Biến thể GPU-instancing của EffectMaterial — dùng shader program RIÊNG
 * (core/shaders/effect_material_instanced.vs, cùng effect_material.fs) nên
 * cần cache uniform location riêng, giống lý do CrystalMaterialInstanced
 * tách khỏi CrystalMaterial (location của cùng 1 tên uniform có thể khác
 * nhau giữa 2 program đã link khác nhau). Dùng với DrawMeshInstanced để vẽ
 * N mesh giống nhau (rock, shard...) bằng ĐÚNG 1 draw call thay vì N lần
 * DrawMesh — xem VFX_ComposeFloatingStones (vc_earth.inl) và
 * CORE_ISSUES.md Item 40. Chỉ dùng cho hiệu ứng backed bởi EffectMaterial
 * (Material_Get/Material_LoadCustom) — hiệu ứng dùng CrystalMaterial thì
 * dùng CrystalMaterialInstanced ở trên thay vì cái này. */
typedef struct
{
    Shader shader;
    MaterialPreset preset;
    int uTimeLoc;
    int uDissolveLoc;
    int uBaseColorLoc;
    int uTranslucencyLoc;
    int uRimStrengthLoc;
    int uFresnelPowerLoc;
    int uEmissiveIntensityLoc;
    int uDistortionStrengthLoc;
    int uHasTexture1Loc;
    int uTexture1Loc;
    EffectMaterialParams params;
} EffectMaterialInstanced;

EffectMaterialInstanced EffectMaterialInstanced_Load(EffectMaterialParams params);
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
    float roughness;    // mapped to u_fresnelPower (fresnelPower = mix(1.0, 8.0, roughness))
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
    int uBaseColorLoc;
    int uEdgeColorLoc;
    int uFresnelPowerLoc;
    int uRimStrengthLoc;
    int uRefractionLoc;
    int uSparkleLoc;
    int uCrackLoc;
    int uEmissionLoc;
    int uThicknessLoc;
    int uDissolveLoc;
    int uTexture1Loc;
    int uTimeLoc;
    int uGrowProgressLoc;
} CrystalMaterial;

CrystalMaterial CrystalMaterial_Load(CrystalMaterialParams params);
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
typedef struct
{
    Shader shader;
    CrystalMaterialParams params;
    int uBaseColorLoc;
    int uEdgeColorLoc;
    int uFresnelPowerLoc;
    int uRimStrengthLoc;
    int uRefractionLoc;
    int uSparkleLoc;
    int uCrackLoc;
    int uEmissionLoc;
    int uThicknessLoc;
    int uDissolveLoc;
    int uTexture1Loc;
    int uTimeLoc;
    int uGrowProgressLoc;
} CrystalMaterialInstanced;

CrystalMaterialInstanced CrystalMaterialInstanced_Load(CrystalMaterialParams params);
void CrystalMaterialInstanced_Begin(CrystalMaterialInstanced mat);
void CrystalMaterialInstanced_SetGrowProgress(CrystalMaterialInstanced mat, float progress);
void CrystalMaterialInstanced_End(void);

/* ============================================================================
 * PLASMA MATERIAL SYSTEM
 * --------------------------------------------------------------------------
 * Wispy energy membrane (plasma_shell.vs/.fs): fresnel × animated fbm alpha,
 * fully transparent at the center — the shell look EffectMaterial's
 * translucency cannot do (it has a 0.3 alpha floor face-on). Draw spheres
 * with it under BLEND_ADDITIVE; backface culling off gives a second, deeper
 * membrane layer for free.
 * ==========================================================================*/

typedef struct
{
    Color baseColor;    // deep body tint of the membrane (alpha = master alpha)
    Color wispColor;    // bright filament-crest tint
    float noiseScale;   // wisp frequency over the sphere (try 2.5-4.0)
    float noiseSpeed;   // wisp scroll speed (try 0.3-0.8)
    float fresnelPower; // rim sharpness 1..8 (higher = emptier center)
    float rimStrength;  // extra rim brightness 0..~2
    float emissive;     // self-illumination boost 0..~2
    float opacity;      // master alpha multiplier 0..1
    float displaceAmp;  // world-units vertex undulation amplitude
} PlasmaMaterialParams;

typedef struct
{
    Shader shader;
    PlasmaMaterialParams params;
    int uBaseColorLoc;
    int uWispColorLoc;
    int uNoiseScaleLoc;
    int uNoiseSpeedLoc;
    int uFresnelPowerLoc;
    int uRimStrengthLoc;
    int uEmissiveLoc;
    int uOpacityLoc;
    int uDisplaceAmpLoc;
    int uTimeLoc;
} PlasmaMaterial;

PlasmaMaterial PlasmaMaterial_Load(PlasmaMaterialParams params);
void PlasmaMaterial_Begin(PlasmaMaterial mat);
void PlasmaMaterial_End(void);

/* ============================================================================
 * AURA SHELL MATERIAL SYSTEM
 * --------------------------------------------------------------------------
 * Cylinder aura shader (aura_shell.vs/.fs): element-tinted fresnel membrane
 * with vertically-scrolling FBM filaments and pulsing horizontal scanline
 * rings. Designed for open-cap cylinder meshes (buff/shield aura columns).
 *
 * bodyColor  = element body/base tint (VFX_Material->body)
 * glowColor  = bright filament/crest color (VFX_Material->glow)
 *
 * Update bodyColor/glowColor each frame before AuraShellMaterial_Begin() so
 * the aura responds to element changes at runtime.
 * ==========================================================================*/

typedef struct
{
    Color bodyColor;    // deep element body tint
    Color glowColor;    // bright filament/crest tint
    float opacity;      // master alpha 0..1
    float fresnelPower; // rim sharpness 1..8 (higher = emptier center)
    float rimStrength;  // extra rim brightness 0..~2
    float scrollSpeed;  // vertical filament scroll speed (positive = up)
    float noiseScale;   // filament horizontal frequency (try 3..6)
    float heightScale;  // vertical stretch of filament pattern (try 1..3)
    float scanFreq;     // horizontal ring pulse frequency (try 6..14)
    float scanSpeed;    // ring upward travel speed (try 0.8..2.0)
    float scanStrength; // ring contribution weight 0..1
    float displaceAmp;  // vertex ripple world-units amplitude
    float topY;         // world Y of cylinder top rim (for top-edge fade)
} AuraShellMaterialParams;

typedef struct
{
    Shader shader;
    AuraShellMaterialParams params;
    int uBodyColorLoc;
    int uGlowColorLoc;
    int uOpacityLoc;
    int uFresnelPowerLoc;
    int uRimStrengthLoc;
    int uScrollSpeedLoc;
    int uNoiseScaleLoc;
    int uHeightScaleLoc;
    int uScanFreqLoc;
    int uScanSpeedLoc;
    int uScanStrengthLoc;
    int uDisplaceAmpLoc;
    int uTopYLoc;
    int uTimeLoc;
} AuraShellMaterial;

AuraShellMaterial AuraShellMaterial_Load(AuraShellMaterialParams params);
void AuraShellMaterial_Begin(AuraShellMaterial mat);
void AuraShellMaterial_End(void);

#endif // MATERIAL_SYSTEM_H
