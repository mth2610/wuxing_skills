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

#endif // MATERIAL_SYSTEM_H
