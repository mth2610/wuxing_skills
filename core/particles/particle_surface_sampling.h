#ifndef WUXING_PARTICLE_SURFACE_SAMPLING_H
#define WUXING_PARTICLE_SURFACE_SAMPLING_H
#include <stdint.h>

/* Deterministic endpoint-inclusive thinning in matching pool order. */
static inline int ParticleSurfaceSampling_Ordinal(int sample, int admitted, int demand)
{
    if (admitted <= 0 || demand <= 0 || sample < 0 || sample >= admitted) return -1;
    if (admitted >= demand) return sample;
    if (admitted == 1) return demand / 2;
    return (int)((int64_t)sample * (demand - 1) / (admitted - 1));
}
#endif
