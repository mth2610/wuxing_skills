#ifndef CORE_EMITTER_GPU_LIFETIME_H
#define CORE_EMITTER_GPU_LIFETIME_H
#include <stdbool.h>
#include <math.h>
/* Conservative completion bound, not a GPU live-particle count. */
typedef struct {
    unsigned int parents;
    float maxLife,remaining;
} EmissionGpuLifetime;
static inline void EmissionGpuLifetime_Bind(EmissionGpuLifetime *s,float life)
{
    s->parents++;s->maxLife=fmaxf(s->maxLife,life);
    s->remaining=fmaxf(s->remaining,s->maxLife);
}
static inline void EmissionGpuLifetime_Retire(EmissionGpuLifetime *s)
{ if(s->parents) s->parents--; }
static inline bool EmissionGpuLifetime_Advance(EmissionGpuLifetime *s,float dt)
{
    if(s->parents) s->remaining=s->maxLife;
    else s->remaining=fmaxf(0,s->remaining-dt);
    if(s->remaining<=0) s->maxLife=0;
    return s->parents || s->remaining>0;
}
#endif
