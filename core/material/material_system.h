#ifndef MATERIAL_SYSTEM_H
#define MATERIAL_SYSTEM_H

#include "raylib.h"

typedef enum {
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

typedef struct {
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
} EffectMaterialParams;

typedef struct {
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
} EffectMaterial;

// Khởi tạo hệ thống chất liệu (Load sẵn Shader/Texture cho các preset)
void MaterialSystem_Init(void);

// Giải phóng hệ thống chất liệu
void MaterialSystem_Unload(void);

// Lấy chất liệu theo preset chuẩn
EffectMaterial Material_Get(MaterialPreset preset);

// Load chất liệu tùy biến
EffectMaterial Material_LoadCustom(EffectMaterialParams params);

// Gán uniform float cho shader
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);

// Bắt đầu dùng chất liệu (đảm bảo an toàn batching của Raylib)
void Material_Begin(EffectMaterial mat);

// Kết thúc dùng chất liệu
void Material_End(void);

/* ============================================================================
 * CRYSTAL MATERIAL SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Một shader duy nhất để vẽ mọi loại tinh thể (Băng, Kim cương, Thạch anh, Ngọc)
 * hỗ trợ các tham số: Fresnel, fake refraction, thickness, sparkles, nứt...
 * ==========================================================================*/

typedef struct {
    Color baseColor;
    Color edgeColor;
    float roughness;           // mapped to u_fresnelPower (fresnelPower = mix(1.0, 8.0, roughness))
    float fresnel;             // mapped to u_rimStrength
    float refraction;          // mapped to u_refraction (distortion)
    float sparkle;             // mapped to u_sparkle (specular sparkle)
    float crack;               // mapped to u_crack (internal noise cracks)
    float emission;            // mapped to u_emission (glow boost)
    float thickness;           // mapped to u_thickness (light absorption)
    float dissolve;            // mapped to u_dissolve (dissolve progress)
    Texture2D texture1;        // detail caustics texture for fake refraction
} CrystalMaterialParams;

typedef struct {
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
} CrystalMaterial;

CrystalMaterial CrystalMaterial_Load(CrystalMaterialParams params);
void CrystalMaterial_Begin(CrystalMaterial mat);
void CrystalMaterial_End(void);

#endif // MATERIAL_SYSTEM_H
