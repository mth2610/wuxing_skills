#ifndef WUXING_PARTICLE_GPU_WORK_GATE_H
#define WUXING_PARTICLE_GPU_WORK_GATE_H

#include <stdbool.h>

/* The compute clock advances even before the first particle is spawned.
 * When running on the CPU/VBO fallback path, the clock also advances once
 * particles have spawned so time-dependent ForceFields and Wind turbulence
 * animate properly instead of freezing at t = 0. */
static inline bool GpuParticleWork_BeginUpdate(bool initialized, bool compute,
                                               bool hasSpawned, float dt,
                                               float *elapsed)
{
    if (!initialized || !elapsed || dt < 0.0f) return false;
    if (compute || hasSpawned) *elapsed += dt;
    return hasSpawned;
}

#endif
