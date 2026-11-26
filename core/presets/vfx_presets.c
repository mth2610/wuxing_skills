#include "vfx_presets.h"
#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/skill_helper.h"
#include "core/decals/decal_system.h"
#include "core/particles/particle_system.h"
#include <stdbool.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// Định nghĩa toàn cục các tài nguyên dùng chung nguyên tố
ColorGradient s_fireGrad;
ColorGradient s_snowGrad;
ColorGradient s_waterGrad;
ColorGradient s_lightningGrad;
ColorGradient s_lightningFollowerGrad;
ColorGradient s_woodGrad;
ColorGradient s_earthGrad;
ColorGradient s_metalGrad;
ColorGradient s_taijiGrad;

ForceField s_fireFld;
ForceField s_snowFld;
ForceField s_waterFld;
ForceField s_lightningFld;
ForceField s_woodFld;
ForceField s_earthFld;
ForceField s_metalFld;
ForceField s_taijiFld;

static bool s_presetsInitialized = false;

// Gradient dành riêng cho impact fire — khởi đầu orange thay vì white
// để phân biệt với ánh sáng trắng của vfx light
static ColorGradient s_fireImpactGrad;

// Gradient cho các material không thuộc 8 nguyên tố gameplay
static ColorGradient s_holyGrad;
static ColorGradient s_voidGrad;
static ColorGradient s_poisonGrad;
static ColorGradient s_qiGradMat;

// Khai báo các mảng preset tĩnh lưu trữ cấu hình
static VFX_ImpactPreset s_ImpactPresets[8];
static VFX_CastPreset s_CastPresets[8];
static VFX_ProjectilePreset s_ProjectilePresets[8];
static VFX_ElementMaterial s_Materials[VC_MAT_COUNT];

void VFX_Presets_Init(void) {
    if (s_presetsInitialized) return;

    // -------------------------------------------------------------
    // Khởi tạo ColorGradients và ForceFields cho các nguyên tố
    // -------------------------------------------------------------

    // 1. Hỏa (Fire) — saturated luminous gold-orange start (spatial shader adds white-hot pin)
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.0f, (Color){ 255, 225, 115, 255 });
    ColorGradient_AddStop(&s_fireGrad, 0.25f, (Color){ 255, 145, 25, 255 });
    ColorGradient_AddStop(&s_fireGrad, 0.7f, (Color){ 210, 45, 8, 255 });
    ColorGradient_AddStop(&s_fireGrad, 1.0f, (Color){ 45, 18, 12, 0 });

    // Fire impact gradient: starts orange-hot so particles contrast against the white vfx light
    s_fireImpactGrad.count = 0;
    ColorGradient_AddStop(&s_fireImpactGrad, 0.0f, (Color){ 255, 200, 60, 255 });
    ColorGradient_AddStop(&s_fireImpactGrad, 0.3f, (Color){ 255, 100, 10, 255 });
    ColorGradient_AddStop(&s_fireImpactGrad, 0.7f, (Color){ 180, 30,  5, 200 });
    ColorGradient_AddStop(&s_fireImpactGrad, 1.0f, (Color){  20, 10,  5,   0 });

    ForceField_Clear(&s_fireFld);
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 18.0f });
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 10.0f, .noiseScale = 0.08f, .noiseSpeed = 1.5f });

    // 2. Băng (Ice/Snow) — Wuxia jewel-tone glacier-cobalt carrier for contrast over snow
    s_snowGrad.count = 0;
    ColorGradient_AddStop(&s_snowGrad, 0.0f, (Color){ 165, 232, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 0.45f, (Color){ 48, 152, 235, 255 });
    ColorGradient_AddStop(&s_snowGrad, 1.0f, (Color){ 20, 72, 148, 0 });

    ForceField_Clear(&s_snowFld);
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.5f, -1.0f, 0.2f}, .strength = 12.0f });
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.1f, .noiseSpeed = 0.8f });

    // 3. Thủy (Water)
    s_waterGrad.count = 0;
    ColorGradient_AddStop(&s_waterGrad, 0.0f, (Color){ 120, 210, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.4f, (Color){ 32, 130, 240, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.8f, (Color){ 10, 56, 172, 180 });
    ColorGradient_AddStop(&s_waterGrad, 1.0f, (Color){ 6, 22, 96, 0 });

    ForceField_Clear(&s_waterFld);
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 35.0f });
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 15.0f, .noiseScale = 0.05f, .noiseSpeed = 2.0f });

    // 4. Lôi (Lightning/Electric) — saturated electric violet-indigo carrier
    s_lightningGrad.count = 0;
    ColorGradient_AddStop(&s_lightningGrad, 0.0f, (Color){ 195, 175, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 0.3f, (Color){ 155, 95, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 0.7f, (Color){ 115, 36, 235, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 1.0f, (Color){ 62, 10, 165, 0 });

    s_lightningFollowerGrad.count = 0;
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.0f, (Color){ 62, 10, 165, 0 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.25f, (Color){ 115, 36, 235, 180 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.6f, (Color){ 165, 120, 255, 255 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 1.0f, (Color){ 215, 200, 255, 255 });

    ForceField_Clear(&s_lightningFld);
    ForceField_AddLayer(&s_lightningFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 45.0f, .noiseScale = 0.2f, .noiseSpeed = 5.0f });

    // 5. Mộc (Wood)
    s_woodGrad.count = 0;
    ColorGradient_AddStop(&s_woodGrad, 0.0f, (Color){ 175, 255, 155, 255 });
    ColorGradient_AddStop(&s_woodGrad, 0.4f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_woodGrad, 1.0f, (Color){ 16, 76, 28, 0 });

    ForceField_Clear(&s_woodFld);
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 10.0f });
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.07f, .noiseSpeed = 1.0f });

    // 6. Thổ (Earth)
    s_earthGrad.count = 0;
    ColorGradient_AddStop(&s_earthGrad, 0.0f, (Color){ 230, 190, 130, 255 });
    ColorGradient_AddStop(&s_earthGrad, 0.5f, ELEMENT_COLOR_EARTH);
    ColorGradient_AddStop(&s_earthGrad, 1.0f, (Color){ 58, 36, 18, 0 });

    ForceField_Clear(&s_earthFld);
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 40.0f });
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.05f, .noiseSpeed = 0.6f });

    // 7. Kim (Metal) — steel-indigo jewel carrier under electric silver-cyan glints
    s_metalGrad.count = 0;
    ColorGradient_AddStop(&s_metalGrad, 0.0f, (Color){ 185, 224, 255, 255 });
    ColorGradient_AddStop(&s_metalGrad, 0.35f, (Color){ 88, 138, 198, 255 });
    ColorGradient_AddStop(&s_metalGrad, 1.0f, (Color){ 36, 52, 78, 0 });

    ForceField_Clear(&s_metalFld);
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 25.0f });
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_DRAG, .strength = 0.5f });

    // 8. Thái Cực (Taiji)
    s_taijiGrad.count = 0;
    ColorGradient_AddStop(&s_taijiGrad, 0.0f, (Color){ 225, 195, 255, 255 });
    ColorGradient_AddStop(&s_taijiGrad, 0.35f, ELEMENT_COLOR_TAIJI);
    ColorGradient_AddStop(&s_taijiGrad, 1.0f, (Color){ 36, 10, 58, 0 });

    ForceField_Clear(&s_taijiFld);
    ForceField_AddLayer(&s_taijiFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.12f, .noiseSpeed = 2.5f });

    // -------------------------------------------------------------
    // Khởi tạo các mảng Presets Va chạm (VFX_ImpactPreset)
    // -------------------------------------------------------------

    // Fire Explosion — 1m-scale. Speed values are direct m/s (no throttle factor).
    // Light kept small (0.3m) and short (0.12s) so it doesn't bleach the particle cloud.
    // Distort disabled — the screen-distort oval visually fights the particle cloud.
    s_ImpactPresets[EFFECT_PRESET_FIRE_EXPLOSION] = (VFX_ImpactPreset){
        .distortEnabled = false,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_BURN, .decalScale = 0.6f, .decalLife = 4.0f,
        .decalTint = (Color){ 60, 25, 5, 200 },
        .lightEnabled = true, .lightColor = (Color){ 255, 140, 20, 255 }, .lightRadius = 0.25f, .lightLife = 0.12f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 70, .countMax = 100, .speedMin = 1.5f, .speedMax = 4.0f,
            .radiusMin = 0.07f, .radiusMax = 0.18f, .lifetimeMin = 0.4f, .lifetimeMax = 1.4f,
            .pitchRange = 1.0f, .upwardBias = 0.8f, .gradient = &s_fireImpactGrad, .forceField = NULL
        }
    };

    // Ice Shatter
    s_ImpactPresets[EFFECT_PRESET_ICE_SHATTER] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.35f, .distortStrength = 0.22f, .distortLife = 0.25f, .distortSpeed = 0.60f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_ICE, .decalScale = 0.18f, .decalLife = 4.0f,
        .lightEnabled = true, .lightColor = (Color){ 140, 210, 255, 255 }, .lightRadius = 0.45f, .lightLife = 0.4f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 30, .countMax = 30, .speedMin = 0.10f, .speedMax = 0.35f,
            .radiusMin = 0.005f, .radiusMax = 0.020f, .lifetimeMin = 0.3f, .lifetimeMax = 1.0f,
            .pitchRange = 0.5f, .upwardBias = 0.20f, .gradient = &s_snowGrad, .forceField = &s_snowFld
        }
    };

    // Water Splash
    s_ImpactPresets[EFFECT_PRESET_WATER_SPLASH] = (VFX_ImpactPreset){
        .distortEnabled = false,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_WATER, .decalScale = 0.15f, .decalLife = 3.5f,
        .lightEnabled = true, .lightColor = (Color){ 80, 180, 255, 255 }, .lightRadius = 0.40f, .lightLife = 0.45f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 35, .countMax = 35, .speedMin = 0.15f, .speedMax = 0.45f,
            .radiusMin = 0.006f, .radiusMax = 0.026f, .lifetimeMin = 0.3f, .lifetimeMax = 1.2f,
            .pitchRange = 0.5f, .upwardBias = 0.35f, .gradient = &s_waterGrad, .forceField = &s_waterFld
        }
    };

    // Lightning Impact
    s_ImpactPresets[EFFECT_PRESET_LIGHTNING_IMPACT] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.45f, .distortStrength = 0.40f, .distortLife = 0.20f, .distortSpeed = 1.5f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_TAIJI_LIGHTNING, .decalScale = 0.14f, .decalLife = 3.0f,
        .lightEnabled = true, .lightColor = (Color){ 200, 150, 255, 255 }, .lightRadius = 0.55f, .lightLife = 0.35f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 25, .countMax = 25, .speedMin = 0.0f, .speedMax = 0.65f,
            .radiusMin = 0.004f, .radiusMax = 0.018f, .lifetimeMin = 0.25f, .lifetimeMax = 0.75f,
            .pitchRange = PI * 0.5f, .upwardBias = 0.37f, .gradient = &s_lightningGrad, .forceField = &s_lightningFld
        }
    };

    // Earth Crack
    s_ImpactPresets[EFFECT_PRESET_EARTH_CRACK] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.30f, .distortStrength = 0.25f, .distortLife = 0.40f, .distortSpeed = 0.50f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_CRACK, .decalScale = 0.25f, .decalLife = 5.5f,
        .lightEnabled = true, .lightColor = (Color){ 180, 140, 100, 255 }, .lightRadius = 0.35f, .lightLife = 0.6f,
        .particlesEnabled = false
    };

    // Wood Bloom
    s_ImpactPresets[EFFECT_PRESET_WOOD_BLOOM] = (VFX_ImpactPreset){
        .distortEnabled = false,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_WOOD_MOSS, .decalScale = 0.18f, .decalLife = 5.0f,
        .lightEnabled = true, .lightColor = ELEMENT_COLOR_WOOD, .lightRadius = 0.35f, .lightLife = 0.45f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 30, .countMax = 30, .speedMin = 0.08f, .speedMax = 0.28f,
            .radiusMin = 0.006f, .radiusMax = 0.022f, .lifetimeMin = 0.5f, .lifetimeMax = 1.5f,
            .pitchRange = 0.5f, .upwardBias = 0.40f, .gradient = &s_woodGrad, .forceField = &s_woodFld
        }
    };

    // Metal Shard
    s_ImpactPresets[EFFECT_PRESET_METAL_SHARD] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.25f, .distortStrength = 0.18f, .distortLife = 0.15f, .distortSpeed = 1.30f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_METAL_SLASH, .decalScale = 0.16f, .decalLife = 4.0f,
        .lightEnabled = true, .lightColor = ELEMENT_COLOR_METAL, .lightRadius = 0.30f, .lightLife = 0.2f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 28, .countMax = 28, .speedMin = 0.35f, .speedMax = 0.95f,
            .radiusMin = 0.003f, .radiusMax = 0.013f, .lifetimeMin = 0.2f, .lifetimeMax = 0.6f,
            .pitchRange = 0.5f, .upwardBias = 0.0f, .gradient = &s_metalGrad, .forceField = &s_metalFld
        }
    };

    // Taiji Burst
    s_ImpactPresets[EFFECT_PRESET_TAIJI_BURST] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.50f, .distortStrength = 0.30f, .distortLife = 0.30f, .distortSpeed = 0.90f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_TAIJI_RING, .decalScale = 0.22f, .decalLife = 5.0f,
        .lightEnabled = true, .lightColor = ELEMENT_COLOR_TAIJI, .lightRadius = 0.70f, .lightLife = 0.55f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 40, .countMax = 40, .speedMin = 0.20f, .speedMax = 0.60f,
            .radiusMin = 0.007f, .radiusMax = 0.025f, .lifetimeMin = 0.4f, .lifetimeMax = 1.3f,
            .pitchRange = 0.5f, .upwardBias = 0.27f, .gradient = &s_taijiGrad, .forceField = &s_taijiFld
        }
    };


    // -------------------------------------------------------------
    // Khởi tạo các mảng Presets Tụ khí (VFX_CastPreset)
    // -------------------------------------------------------------

    s_CastPresets[EFFECT_PRESET_FIRE_EXPLOSION] = (VFX_CastPreset){
        .flashColor = (Color){ 255, 120, 20, 255 }, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_fireGrad
    };

    s_CastPresets[EFFECT_PRESET_ICE_SHATTER] = (VFX_CastPreset){
        .flashColor = (Color){ 140, 210, 255, 255 }, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_snowGrad
    };

    s_CastPresets[EFFECT_PRESET_WATER_SPLASH] = (VFX_CastPreset){
        .flashColor = (Color){ 80, 180, 255, 255 }, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_waterGrad
    };

    s_CastPresets[EFFECT_PRESET_LIGHTNING_IMPACT] = (VFX_CastPreset){
        .flashColor = (Color){ 200, 150, 255, 255 }, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_lightningGrad
    };

    s_CastPresets[EFFECT_PRESET_EARTH_CRACK] = (VFX_CastPreset){
        .flashColor = ELEMENT_COLOR_EARTH, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_earthGrad
    };

    s_CastPresets[EFFECT_PRESET_WOOD_BLOOM] = (VFX_CastPreset){
        .flashColor = ELEMENT_COLOR_WOOD, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_woodGrad
    };

    s_CastPresets[EFFECT_PRESET_METAL_SHARD] = (VFX_CastPreset){
        .flashColor = ELEMENT_COLOR_METAL, .lightRadius = 0.40f, .lightLifetime = 0.5f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_metalGrad
    };

    s_CastPresets[EFFECT_PRESET_TAIJI_BURST] = (VFX_CastPreset){
        .flashColor = ELEMENT_COLOR_TAIJI, .lightRadius = 0.55f, .lightLifetime = 0.65f,
        .particleCount = 20, .spawnRadius = 0.18f, .pullStrength = 0.60f, .gradient = &s_taijiGrad
    };


    // -------------------------------------------------------------
    // Khởi tạo các mảng Presets Đạn bay (VFX_ProjectilePreset)
    // -------------------------------------------------------------

    s_ProjectilePresets[EFFECT_PRESET_FIRE_EXPLOSION] = (VFX_ProjectilePreset){
        .tint = (Color){ 255, 120, 20, 255 }, .gradient = &s_fireGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_ICE_SHATTER] = (VFX_ProjectilePreset){
        .tint = (Color){ 140, 210, 255, 255 }, .gradient = &s_snowGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_WATER_SPLASH] = (VFX_ProjectilePreset){
        .tint = (Color){ 80, 180, 255, 255 }, .gradient = &s_waterGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_LIGHTNING_IMPACT] = (VFX_ProjectilePreset){
        .tint = (Color){ 200, 150, 255, 255 }, .gradient = &s_lightningGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_EARTH_CRACK] = (VFX_ProjectilePreset){
        .tint = ELEMENT_COLOR_EARTH, .gradient = &s_earthGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_WOOD_BLOOM] = (VFX_ProjectilePreset){
        .tint = ELEMENT_COLOR_WOOD, .gradient = &s_woodGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_METAL_SHARD] = (VFX_ProjectilePreset){
        .tint = ELEMENT_COLOR_METAL, .gradient = &s_metalGrad
    };

    s_ProjectilePresets[EFFECT_PRESET_TAIJI_BURST] = (VFX_ProjectilePreset){
        .tint = ELEMENT_COLOR_TAIJI, .gradient = &s_taijiGrad
    };

    // -------------------------------------------------------------
    // Gradient cho các material ngoài 8 nguyên tố gameplay
    // -------------------------------------------------------------

    s_holyGrad.count = 0;
    ColorGradient_AddStop(&s_holyGrad, 0.0f, (Color){ 255, 250, 214, 255 });
    ColorGradient_AddStop(&s_holyGrad, 0.4f, (Color){ 255, 232, 134, 255 });
    ColorGradient_AddStop(&s_holyGrad, 1.0f, (Color){ 186, 148, 48, 0 });

    s_voidGrad.count = 0;
    ColorGradient_AddStop(&s_voidGrad, 0.0f, (Color){ 205, 145, 255, 255 });
    ColorGradient_AddStop(&s_voidGrad, 0.4f, (Color){ 135, 32, 198, 255 });
    ColorGradient_AddStop(&s_voidGrad, 1.0f, (Color){ 36, 4, 68, 0 });

    s_poisonGrad.count = 0;
    ColorGradient_AddStop(&s_poisonGrad, 0.0f, (Color){ 185, 255, 145, 255 });
    ColorGradient_AddStop(&s_poisonGrad, 0.4f, (Color){ 105, 212, 72, 255 });
    ColorGradient_AddStop(&s_poisonGrad, 1.0f, (Color){ 32, 82, 24, 0 });

    s_qiGradMat.count = 0;
    ColorGradient_AddStop(&s_qiGradMat, 0.0f, (Color){ 125, 215, 255, 255 });
    ColorGradient_AddStop(&s_qiGradMat, 0.4f, (Color){ 16, 135, 238, 255 });
    ColorGradient_AddStop(&s_qiGradMat, 1.0f, (Color){ 12, 52, 138, 0 });

    // -------------------------------------------------------------
    // Bảng material nguyên tố — nguồn sự thật cho tầng composition.
    // body = màu bản sắc (shell/ribbon/rune), glow = điểm nóng phát sáng.
    // Giá trị hội tụ từ các switch arm cũ trong core/composition/*.inl.
    // -------------------------------------------------------------

    s_Materials[VC_MAT_FIRE] = (VFX_ElementMaterial){
        .body = ELEMENT_COLOR_FIRE, .glow = (Color){ 255, 90, 20, 255 },
        .soft = (Color){ 255, 110, 30, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_fireGrad, .hotGrad = &s_fireGrad,
        .fld = &s_fireFld, .runeDecal = "assets/textures/decals/decal_lava_crack.png",
        .decalMaterial = DECAL_MATERIAL_SCORCH
    };
    s_Materials[VC_MAT_ICE] = (VFX_ElementMaterial){
        .body = (Color){ 38, 128, 218, 255 }, .glow = (Color){ 120, 212, 255, 255 },
        .soft = (Color){ 140, 215, 255, 255 },
        .blendMode = BLEND_ALPHA, .grad = &s_snowGrad, .hotGrad = &s_snowGrad,
        .fld = &s_snowFld, .runeDecal = "assets/textures/decals/decal_crack.png",
        .decalMaterial = DECAL_MATERIAL_FROST
    };
    s_Materials[VC_MAT_WATER] = (VFX_ElementMaterial){
        .body = ELEMENT_COLOR_WATER, .glow = (Color){ 60, 168, 255, 255 },
        .soft = (Color){ 140, 215, 255, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_waterGrad, .hotGrad = &s_waterGrad,
        .fld = &s_waterFld, .runeDecal = "assets/textures/decals/decal_water_ripple.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    // Lightning có hai bản sắc cùng tồn tại: glow cyan điện cho hồ quang/beam,
    // body tím cho ambient (khớp s_lightningGrad + aura cũ).
    s_Materials[VC_MAT_LIGHTNING] = (VFX_ElementMaterial){
        .body = (Color){ 155, 38, 245, 255 }, .glow = (Color){ 0, 175, 255, 255 },
        .soft = (Color){ 165, 95, 255, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_lightningGrad, .hotGrad = &s_lightningFollowerGrad,
        .fld = &s_lightningFld, .runeDecal = "assets/textures/decals/decal_metal_rune.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_EARTH] = (VFX_ElementMaterial){
        .body = ELEMENT_COLOR_EARTH, .glow = (Color){ 180, 140, 100, 255 },
        .soft = (Color){ 215, 170, 115, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_earthGrad, .hotGrad = &s_earthGrad,
        .fld = &s_earthFld, .runeDecal = "assets/textures/decals/decal_earth_rune.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_WOOD] = (VFX_ElementMaterial){
        .body = ELEMENT_COLOR_WOOD, .glow = (Color){ 0, 220, 82, 255 },
        .soft = (Color){ 88, 215, 128, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_woodGrad, .hotGrad = &s_woodGrad,
        .fld = &s_woodFld, .runeDecal = "assets/textures/decals/decal_root_mark.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    // Metal: steel-indigo body carrier for snow readability, electric azure-silver glow
    s_Materials[VC_MAT_METAL] = (VFX_ElementMaterial){
        .body = (Color){ 52, 82, 124, 255 }, .glow = (Color){ 105, 192, 255, 255 },
        .soft = (Color){ 190, 224, 255, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_metalGrad, .hotGrad = &s_metalGrad,
        .fld = &s_metalFld, .runeDecal = "assets/textures/decals/decal_metal_rune.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    // Taiji: body tím, glow vàng gold (giữ aura gold có chủ ý của AURA_TAIJI cũ).
    s_Materials[VC_MAT_TAIJI] = (VFX_ElementMaterial){
        .body = ELEMENT_COLOR_TAIJI, .glow = (Color){ 255, 175, 0, 255 },
        .soft = (Color){ 200, 228, 255, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_taijiGrad, .hotGrad = &s_taijiGrad,
        .fld = &s_taijiFld, .runeDecal = "assets/textures/decals/decal_taiji_ring.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_HOLY] = (VFX_ElementMaterial){
        .body = (Color){ 214, 178, 72, 255 }, .glow = (Color){ 255, 240, 158, 255 },
        .soft = (Color){ 255, 249, 212, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_holyGrad, .hotGrad = &s_holyGrad,
        .fld = &s_taijiFld, .runeDecal = "assets/textures/decals/decal_taiji_ring.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_VOID] = (VFX_ElementMaterial){
        .body = (Color){ 120, 20, 200, 255 }, .glow = (Color){ 140, 40, 200, 255 },
        .soft = (Color){ 190, 130, 255, 255 },
        .blendMode = BLEND_ALPHA, .grad = &s_voidGrad, .hotGrad = &s_voidGrad,
        .fld = &s_taijiFld, .runeDecal = "assets/textures/decals/decal_taiji_ring.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_POISON] = (VFX_ElementMaterial){
        .body = (Color){ 92, 198, 62, 255 }, .glow = (Color){ 115, 220, 82, 255 },
        .soft = (Color){ 165, 235, 135, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_poisonGrad, .hotGrad = &s_poisonGrad,
        .fld = &s_woodFld, .runeDecal = "assets/textures/decals/decal_root_mark.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };
    s_Materials[VC_MAT_QI] = (VFX_ElementMaterial){
        .body = (Color){ 18, 108, 210, 255 }, .glow = (Color){ 0, 165, 255, 255 },
        .soft = (Color){ 180, 225, 255, 255 },
        .blendMode = BLEND_ALPHA_PREMULTIPLY, .grad = &s_qiGradMat, .hotGrad = &s_qiGradMat,
        .fld = &s_woodFld, .runeDecal = "assets/textures/decals/decal_taiji_ring.png",
        .decalMaterial = DECAL_MATERIAL_IMPACT
    };

    s_presetsInitialized = true;
}

const VFX_ImpactPreset* VFX_Preset_GetImpact(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return NULL;
    VFX_Presets_Init();
    return &s_ImpactPresets[preset];
}

const VFX_CastPreset* VFX_Preset_GetCast(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return NULL;
    VFX_Presets_Init();
    return &s_CastPresets[preset];
}

const VFX_ProjectilePreset* VFX_Preset_GetProjectile(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return NULL;
    VFX_Presets_Init();
    return &s_ProjectilePresets[preset];
}

const VFX_ElementMaterial* VFX_Material(VC_MaterialId id) {
    VFX_Presets_Init();
    if (id < 0 || id >= VC_MAT_COUNT) id = VC_MAT_TAIJI;
    return &s_Materials[id];
}

VC_MaterialId VFX_MaterialFromPreset(EffectPresetType preset) {
    switch (preset) {
        case EFFECT_PRESET_FIRE_EXPLOSION:   return VC_MAT_FIRE;
        case EFFECT_PRESET_ICE_SHATTER:      return VC_MAT_ICE;
        case EFFECT_PRESET_WATER_SPLASH:     return VC_MAT_WATER;
        case EFFECT_PRESET_LIGHTNING_IMPACT: return VC_MAT_LIGHTNING;
        case EFFECT_PRESET_EARTH_CRACK:      return VC_MAT_EARTH;
        case EFFECT_PRESET_WOOD_BLOOM:       return VC_MAT_WOOD;
        case EFFECT_PRESET_METAL_SHARD:      return VC_MAT_METAL;
        case EFFECT_PRESET_TAIJI_BURST:      return VC_MAT_TAIJI;
        default:                             return VC_MAT_TAIJI;
    }
}
