#ifndef CORE_EMITTER_PARTICLE_CHILDREN_H
#define CORE_EMITTER_PARTICLE_CHILDREN_H
#include "core/emitter/emission_events.h"
#include "core/particles/particle_system.h"
#define EMISSION_PARTICLE_PARENT_CAPACITY 2000
#define EMISSION_PARTICLE_EVENT_BUDGET 256
typedef uint64_t EmissionChildrenHandle;
typedef void (*EmissionChildSpawnFn)(void *user, ParticleConfig child);
typedef float (*EmissionChildRandomFn)(void *user);
typedef struct {
    uint64_t submitted, dropped;
} EmissionChildrenStats;
/* CPU compatibility policy owner. Templates are copied at Bind; referenced
 * curves, fields and textures must outlive children. Parent slots have the
 * same bounded index as the CPU component pool; generation rejects reuse.
 * One child generation: copied templates have all child links cleared.
 * LIVE preserves the legacy ten-birth cap and discards excess backlog.
 * Other events submit at most 256 children, consuming/counting overflow.
 * Dispatch is synchronous; callback must not reset/rebind this parent. */
void EmissionChildren_Init(EmissionChildSpawnFn spawn,EmissionChildRandomFn random,void *user);
EmissionChildrenHandle EmissionChildren_Bind(int parentSlot,const ParticleConfig *parent);
void EmissionChildren_Release(EmissionChildrenHandle handle);
void EmissionChildren_Publish(EmissionChildrenHandle handle,const EmissionEvent *event);
EmissionChildrenStats EmissionChildren_GetStats(void);

/* Legacy GPU travel's CPU event bridge. Emitter owns 64 copied impact
 * templates; the component retains only a generation-checked binding. This
 * is CPU event delivery, never a readback-based GPU child-emission path. */
void EmissionImpact_Init(void);
EmissionChildrenHandle EmissionImpact_Retain(const ParticleConfig *config);
void EmissionImpact_Release(EmissionChildrenHandle handle);
void EmissionImpact_Publish(EmissionChildrenHandle handle,int count,const EmissionEvent *event);
typedef int (*EmissionRandomIntFn)(int min,int max,void *user);
/* Historical GPU CPU-shadow dust policy, preserving its authored appearance
 * and three births. Backend supplies RNG only; Emitter owns the template. */
void EmissionLegacy_CollisionDust(Vector3 position,EmissionRandomIntFn random,void *user);
#endif
