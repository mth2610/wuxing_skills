/*
 * ============================================================================
 * IMPACT BURST - Implementation
 * ----------------------------------------------------------------------------
 * New file: core/impact_burst.c. Register it the same way as other core
 * modules in your build (CMakeLists.txt / generate_registry.py source list).
 *
 * Math/ordering carried over 1:1 from TriggerWaterBurst() in the original
 * tube_skill.c, parametrized through ImpactBurstConfig.
 *
 * NOTE ON ENCODING: ASCII-only comments. See core/procedural_mesh_utils.h.
 * ==========================================================================*/

#include "core/impact_burst.h"
#include "core/screen_distort.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/particle_radial_burst.h"
#include "raylib.h"
#include <stddef.h>

void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg) {
    if (cfg == NULL) return;

    /* Step 1: screen distortion */
    if (cfg->distortEnabled) {
        ScreenDistort_Add(pos, cfg->distortRadius, cfg->distortStrength,
                          cfg->distortLife, cfg->distortSpeed);
    }

    /* Step 2: ground decal */
    if (cfg->decalEnabled) {
        float rotation = cfg->decalRandomRotation
            ? (float)GetRandomValue(0, 360)
            : cfg->decalFixedRotation;
        DecalSystem_Add(pos, rotation, cfg->decalScale * sizeScale,
                        cfg->decalTex, cfg->decalLife, cfg->decalTint);
    }

    /* Step 3: point light flash */
    if (cfg->lightEnabled) {
        VFXLight_Spawn(pos, cfg->lightColor, cfg->lightRadius * sizeScale, cfg->lightLife, VFX_PRIORITY_LOW);
    }

    /* Step 4: radial particle burst */
    if (cfg->particlesEnabled) {
        ParticleSystem_SpawnRadialBurst(pos, sizeScale, &cfg->particles);
    }
}
