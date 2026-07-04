#include "vfx_presets.h"
#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/skill_helper.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
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

// Khai báo các mảng preset tĩnh lưu trữ cấu hình
static VFX_ImpactPreset s_ImpactPresets[8];
static VFX_CastPreset s_CastPresets[8];
static VFX_ProjectilePreset s_ProjectilePresets[8];

void VFX_Presets_Init(void) {
    if (s_presetsInitialized) return;

    // -------------------------------------------------------------
    // Khởi tạo ColorGradients và ForceFields cho các nguyên tố
    // -------------------------------------------------------------

    // 1. Hỏa (Fire)
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_fireGrad, 0.2f, (Color){ 255, 180, 50, 255 });
    ColorGradient_AddStop(&s_fireGrad, 0.7f, (Color){ 230, 60, 10, 255 });
    ColorGradient_AddStop(&s_fireGrad, 1.0f, (Color){ 30, 30, 30, 0 });

    ForceField_Clear(&s_fireFld);
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 18.0f });
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 10.0f, .noiseScale = 0.08f, .noiseSpeed = 1.5f });

    // 2. Băng (Ice/Snow)
    s_snowGrad.count = 0;
    ColorGradient_AddStop(&s_snowGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_snowGrad, 0.5f, (Color){ 180, 220, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 1.0f, (Color){ 150, 200, 255, 0 });

    ForceField_Clear(&s_snowFld);
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.5f, -1.0f, 0.2f}, .strength = 12.0f });
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.1f, .noiseSpeed = 0.8f });

    // 3. Thủy (Water)
    s_waterGrad.count = 0;
    ColorGradient_AddStop(&s_waterGrad, 0.0f, (Color){ 200, 230, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.4f, (Color){ 50, 150, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.8f, (Color){ 10, 60, 180, 180 });
    ColorGradient_AddStop(&s_waterGrad, 1.0f, (Color){ 5, 20, 100, 0 });

    ForceField_Clear(&s_waterFld);
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 35.0f });
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 15.0f, .noiseScale = 0.05f, .noiseSpeed = 2.0f });

    // 4. Lôi (Lightning/Electric)
    s_lightningGrad.count = 0;
    ColorGradient_AddStop(&s_lightningGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_lightningGrad, 0.3f, (Color){ 220, 200, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 0.7f, (Color){ 140, 50, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 1.0f, (Color){ 80, 10, 200, 0 });

    s_lightningFollowerGrad.count = 0;
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.0f, (Color){ 80, 10, 200, 0 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.25f, (Color){ 140, 50, 255, 180 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.6f, (Color){ 220, 200, 255, 255 });
    ColorGradient_AddStop(&s_lightningFollowerGrad, 1.0f, WHITE);

    ForceField_Clear(&s_lightningFld);
    ForceField_AddLayer(&s_lightningFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 45.0f, .noiseScale = 0.2f, .noiseSpeed = 5.0f });

    // 5. Mộc (Wood)
    s_woodGrad.count = 0;
    ColorGradient_AddStop(&s_woodGrad, 0.0f, (Color){ 220, 255, 200, 255 });
    ColorGradient_AddStop(&s_woodGrad, 0.4f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_woodGrad, 1.0f, (Color){ 20, 80, 30, 0 });

    ForceField_Clear(&s_woodFld);
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 10.0f });
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.07f, .noiseSpeed = 1.0f });

    // 6. Thổ (Earth)
    s_earthGrad.count = 0;
    ColorGradient_AddStop(&s_earthGrad, 0.0f, (Color){ 230, 200, 160, 255 });
    ColorGradient_AddStop(&s_earthGrad, 0.5f, ELEMENT_COLOR_EARTH);
    ColorGradient_AddStop(&s_earthGrad, 1.0f, (Color){ 60, 40, 20, 0 });

    ForceField_Clear(&s_earthFld);
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 40.0f });
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.05f, .noiseSpeed = 0.6f });

    // 7. Kim (Metal)
    s_metalGrad.count = 0;
    ColorGradient_AddStop(&s_metalGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_metalGrad, 0.3f, ELEMENT_COLOR_METAL);
    ColorGradient_AddStop(&s_metalGrad, 1.0f, (Color){ 60, 60, 65, 0 });

    ForceField_Clear(&s_metalFld);
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 25.0f });
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_DRAG, .strength = 0.5f });

    // 8. Thái Cực (Taiji)
    s_taijiGrad.count = 0;
    ColorGradient_AddStop(&s_taijiGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_taijiGrad, 0.35f, ELEMENT_COLOR_TAIJI);
    ColorGradient_AddStop(&s_taijiGrad, 1.0f, (Color){ 40, 10, 60, 0 });

    ForceField_Clear(&s_taijiFld);
    ForceField_AddLayer(&s_taijiFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.12f, .noiseSpeed = 2.5f });

    // -------------------------------------------------------------
    // Khởi tạo các mảng Presets Va chạm (VFX_ImpactPreset)
    // -------------------------------------------------------------

    // Fire Explosion
    s_ImpactPresets[EFFECT_PRESET_FIRE_EXPLOSION] = (VFX_ImpactPreset){
        .distortEnabled = true, .distortRadius = 0.55f, .distortStrength = 0.35f, .distortLife = 0.35f, .distortSpeed = 1.0f,
        .decalEnabled = true, .decalPreset = DECAL_PRESET_BURN, .decalScale = 0.22f, .decalLife = 5.0f,
        .lightEnabled = true, .lightColor = (Color){ 255, 120, 20, 255 }, .lightRadius = 0.65f, .lightLife = 0.5f,
        .particlesEnabled = true,
        .particles = {
            .countMin = 45, .countMax = 45, .speedMin = 0.20f, .speedMax = 0.55f,
            .radiusMin = 0.008f, .radiusMax = 0.026f, .lifetimeMin = 0.4f, .lifetimeMax = 1.2f,
            .pitchRange = 0.5f, .upwardBias = 0.30f, .gradient = &s_fireGrad, .forceField = &s_fireFld
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
