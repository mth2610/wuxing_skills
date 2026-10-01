#ifndef WUXING_PARTICLE_GPU_WORK_GATE_H
#define WUXING_PARTICLE_GPU_WORK_GATE_H

#include <stdbool.h>

/* The compute clock advances even before the first particle is spawned.
 * No lifetime estimate is sufficient to declare a previously used GPU pool
 * empty: this gate only admits work once a spawn has occurred since Init. */
static inline bool GpuParticleWork_BeginUpdate(bool initialized, bool compute,
                                               bool hasSpawned, float dt,
                                               float *elapsed)
{
    if (!initialized) return false;
    if (compute) *elapsed += dt;
    return hasSpawned;
}

#endif
