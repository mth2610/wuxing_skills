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

// -------------------------------------------------------------
// 4. Bảng material nguyên tố (VFX_ElementMaterial)
// Nguồn sự thật duy nhất cho "nguyên tố X trông thế nào" ở tầng
// composition: đổi look một nguyên tố = sửa một entry ở đây.
// -------------------------------------------------------------

typedef enum {
    VC_MAT_FIRE,
    VC_MAT_ICE,
    VC_MAT_WATER,
    VC_MAT_LIGHTNING,
    VC_MAT_EARTH,
    VC_MAT_WOOD,
    VC_MAT_METAL,
    VC_MAT_TAIJI,
    VC_MAT_HOLY,
    VC_MAT_VOID,
    VC_MAT_POISON,
    VC_MAT_QI,
    VC_MAT_COUNT
} VC_MaterialId;

typedef struct {
    Color body;                   // màu bản sắc nguyên tố (shell, ribbon, rune)
    Color glow;                   // màu điểm nóng phát sáng (beam, ember)
    Color soft;                   // pastel nhạt cho aura/VFXLight/glint (bảng qi aura cũ của vc_summon)
    int   blendMode;              // blend khuyến nghị cho layer sheet/beam (BLEND_ADDITIVE/BLEND_ALPHA)
    const ColorGradient *grad;    // gradient hạt chuẩn của nguyên tố
    const ColorGradient *hotGrad; // biến thể sáng hơn (đảo chiều); nếu không có thì trỏ về grad
    const ForceField *fld;        // trường lực chuẩn của nguyên tố
    const char *runeDecal;        // texture vòng rune dưới đất (shield/charge)
} VFX_ElementMaterial;

// Luôn trả về entry hợp lệ (id sai → VC_MAT_TAIJI); mọi con trỏ trong entry đều non-NULL.
const VFX_ElementMaterial* VFX_Material(VC_MaterialId id);

// Map 8 element preset (skill-facing) sang material tương ứng.
VC_MaterialId VFX_MaterialFromPreset(EffectPresetType preset);

// Gắn alpha lên màu material tại call site — material chỉ giữ RGB nhận diện,
// cường độ (alpha) là quyết định của từng layer.
static inline Color VC_WithAlpha(Color c, unsigned char a) { c.a = a; return c; }

// Khởi tạo thư viện presets (gradients, force fields, configs)
void VFX_Presets_Init(void);

// Hàm truy vấn các preset cấu hình nguyên tố
const VFX_ImpactPreset* VFX_Preset_GetImpact(EffectPresetType preset);
const VFX_CastPreset* VFX_Preset_GetCast(EffectPresetType preset);
const VFX_ProjectilePreset* VFX_Preset_GetProjectile(EffectPresetType preset);

#endif // CORE_VFX_PRESETS_H
