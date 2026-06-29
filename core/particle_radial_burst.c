/*
 * ============================================================================
 * PARTICLE RADIAL BURST - Implementation
 * ----------------------------------------------------------------------------
 * Append this to the end of the existing core/particle_system.c, alongside
 * #include "core/particle_radial_burst.h".
 *
 * Math carried over 1:1 from TriggerWaterBurst() in the original
 * tube_skill.c, parametrized through ParticleRadialBurstConfig.
 *
 * NOTE ON ENCODING: ASCII-only comments. See core/procedural_mesh_utils.h.
 * ==========================================================================*/

#include "core/particle_radial_burst.h"
#include "core/utils_math.h"
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.1415926535f
#endif

void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg) {
    if (cfg == NULL) return;

    int count = GetRandomValue(cfg->countMin, cfg->countMax);
    count = (int)((float)count * sizeScale);

    for (int s = 0; s < count; s++) {
        float angle = Random01() * PI * 2.0f;
        /* pitchRange = 0 -> always 0 (flat burst). pitchRange = PI -> full sphere. */
        float pitch = (Random01() - 0.5f) * cfg->pitchRange;
        float speed = Math_Mix(cfg->speedMin, cfg->speedMax, Random01()) * sizeScale;

        ParticleConfig pcfg = {0};
        pcfg.position = origin;
        pcfg.velocity = (Vector3){
            cosf(angle) * speed * cosf(pitch),
            sinf(pitch) * speed + (cfg->upwardBias * sizeScale),
            sinf(angle) * speed * cosf(pitch)
        };
        pcfg.radius = Math_Mix(cfg->radiusMin, cfg->radiusMax, Random01()) * sizeScale;
        pcfg.lifetime = Math_Mix(cfg->lifetimeMin, cfg->lifetimeMax, Random01());
        pcfg.colorStart = cfg->colorStart;
        pcfg.colorEnd = cfg->colorEnd;
        pcfg.forceField = cfg->forceField;
        pcfg.gradient = cfg->gradient;

        SpawnParticle(pcfg);
    }
}
