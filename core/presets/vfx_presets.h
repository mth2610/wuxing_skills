#ifndef CORE_VFX_PRESETS_H
#define CORE_VFX_PRESETS_H

#include "raylib.h"
#include "core/skill_helper.h" // for EffectPresetType
#include "core/decal_system.h" // for DecalPresetType
#include "core/particle_system.h" // for ParticleRadialBurstConfig

// 1. Cấu hình preset va chạm (Impact)
typedef struct {
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;
    
    bool            decalEnabled;
    DecalPresetType decalPreset;
    float           decalScale, decalLife;
    
    bool  lightEnabled;
    Color lightColor;
    float lightRadius, lightLife;
    
    bool                      particlesEnabled;
    ParticleRadialBurstConfig particles;
} VFX_ImpactPreset;

// 2. Cấu hình preset tụ khí (Cast)
typedef struct {
    Color flashColor;
    float lightRadius, lightLifetime;
    int   particleCount;
    float spawnRadius;
    float pullStrength;
    const ColorGradient *gradient;
} VFX_CastPreset;

// 3. Cấu hình preset đạn bay (Projectile Trail)
typedef struct {
    Color tint;
    const ColorGradient *gradient;
} VFX_ProjectilePreset;

// Khai báo các tài nguyên nguyên tố toàn cục dùng chung
extern ColorGradient s_fireGrad;
extern ColorGradient s_snowGrad;
extern ColorGradient s_waterGrad;
extern ColorGradient s_lightningGrad;
extern ColorGradient s_lightningFollowerGrad;
extern ColorGradient s_woodGrad;
extern ColorGradient s_earthGrad;
extern ColorGradient s_metalGrad;
extern ColorGradient s_taijiGrad;

extern ForceField s_fireFld;
extern ForceField s_snowFld;
extern ForceField s_waterFld;
extern ForceField s_lightningFld;
extern ForceField s_woodFld;
extern ForceField s_earthFld;
extern ForceField s_metalFld;
extern ForceField s_taijiFld;

// Khởi tạo thư viện presets (gradients, force fields, configs)
void VFX_Presets_Init(void);

// Hàm truy vấn các preset cấu hình nguyên tố
const VFX_ImpactPreset* VFX_Preset_GetImpact(EffectPresetType preset);
const VFX_CastPreset* VFX_Preset_GetCast(EffectPresetType preset);
const VFX_ProjectilePreset* VFX_Preset_GetProjectile(EffectPresetType preset);

#endif // CORE_VFX_PRESETS_H
