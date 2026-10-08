#ifndef CORE_EMITTER_GPU_H
#define CORE_EMITTER_GPU_H
#include "core/particles/particle_system.h"
#include "core/motion/motion_gpu.h"
#include <math.h>

/* Visual leaf births only. CPU handles recursive, collision and arrival
 * emitters. Vulkan-only; other backends use CPU fallback. No GPU readback,
 * surface-input routing or completion query. GPU cadence caps at 64 per parent
 * per dispatch and uses the post-integration position; legacy CPU cadence keeps
 * its original interpolation and 10-birth cap. These policies differ. */
#define EMISSION_GPU_CHILD_CAPACITY 2048
#define EMISSION_GPU_TEMPLATE_CAPACITY 64
#define EMISSION_GPU_PARENT_CAPACITY 8192
typedef struct { Vector4 data[9]; } EmissionGpuParticle;
typedef struct {
    EmissionGpuParticle particle;
    MotionGpuBody body;
    Vector4 inheritance;
} EmissionGpuTemplate;
typedef struct {
    unsigned int meta[4]; /* generation, live template+1, death template+1, count */
    Vector4 timing; /* live rate, fractional timer, armed, reserved */
} EmissionGpuParent;

static inline bool EmissionGpu_LeafSupported(const ParticleConfig *source)
{
    if (!source) return false;
    ParticleConfig p=*source; ParticleConfig_Unify(&p);
    return isfinite(p.lifetime) && p.lifetime>0 && isfinite(p.radius) && p.radius>0 &&
        p.physics.spatialMotionOnly && !p.physics.initialGuide && !p.travelPath &&
        !p.forceField && !p.physics.followTarget && !p.onDeathEmit && !p.onLiveEmit &&
        !p.onTargetEmitCount && !p.physics.onCollisionEmitCount &&
        !p.gradient && !p.spriteAnim && !p.render.shader.id && !p.render.texture.id &&
        !p.radiusCurve && !p.speedCurve && !p.alphaCurve && !p.emissiveCurve &&
        !p.rotation && !p.angularVelocity && !p.trailLength && !p.meshModel.meshCount &&
        !p.spriteFlipX && !p.spriteFlipY && !p.render.volumeSheet &&
        p.facingMode<=VFX_FACING_VELOCITY &&
        p.render.appearance==VFX_APPEARANCE_INHERIT && p.render.contrastProfile==VFX_CONTRAST_NONE &&
        (p.render.blendMode==VFX_BLEND_ALPHA || p.render.blendMode==VFX_BLEND_ADDITIVE);
}
static inline bool EmissionGpu_ConfigSupported(const ParticleConfig *p)
{
    if(!p) return false;
    if((p->onDeathEmitCount || p->onLiveEmitRate) && !p->physics.spatialMotionOnly) return false;
    return (!p->onDeathEmitCount || (p->onDeathEmitCount>0 && p->onDeathEmitCount<=64 &&
        EmissionGpu_LeafSupported(p->onDeathEmit))) &&
        (!p->onLiveEmitRate || (isfinite(p->onLiveEmitRate) && p->onLiveEmitRate>0 &&
        EmissionGpu_LeafSupported(p->onLiveEmit)));
}

bool EmissionGpu_Init(void);
void EmissionGpu_Unload(void);
/* Slot replacement resets cadence and arms precisely the new resident parent.
 * Templates/profiles are copied; no caller pointer survives this call. */
bool EmissionGpu_SetParent(unsigned int slot,const ParticleConfig *config,int owner);
void EmissionGpu_ReleaseParent(unsigned int slot);
void EmissionGpu_Dispatch(unsigned int parentBuffer,unsigned int parentCount,float dt);
unsigned int EmissionGpu_ChildBuffer(void);
unsigned int EmissionGpu_ChildBodyBuffer(void);
bool EmissionGpu_HasChildren(void); /* Conservative since last reset. */
unsigned int EmissionGpu_BlendMask(void);
#endif
