#ifndef CORE_TRAIL_RIBBON_GPU_H
#define CORE_TRAIL_RIBBON_GPU_H
#include "core/trails/trail_ribbon_solver.h"

#define TRAIL_RIBBON_GPU_CAPACITY 64
/* std430 ABI v1; persistent nodes/body lanes are uploaded only at spawn.
 * One workgroup owns each bounded chain. Node Motion evaluation is parallel;
 * ordered constraints stay on lane zero, with shared-memory barriers. */
typedef struct {
    Vector4 positionRest, velocity, previous;
} TrailRibbonGpuNode;
typedef struct {
    unsigned int meta[4]; /* count, iterations, head-anchored, active */
    Vector4 compliance;  /* stretch, bend, pending release velocity, reserved */
    Vector4 anchor;      /* xyz target, w valid */
    Vector4 anchorVelocity; /* xyz velocity, w discontinuity */
} TrailRibbonGpuParams;
typedef char TrailRibbonGpuNodeLayout[(sizeof(TrailRibbonGpuNode)==48)?1:-1];
typedef char TrailRibbonGpuParamsLayout[(sizeof(TrailRibbonGpuParams)==64)?1:-1];

/* Lazy device initialization. False means the modern CPU fallback is needed.
 * BeginUpdate uploads one shared scene/wind snapshot; Update stages controls.
 * EndUpdate submits one dispatch for all active slots after lifetime/attachment updates.
 * GetState is intentionally unavailable: GPU positions stay GPU-owned. */
bool TrailRibbonGpu_IsAvailable(void);
int TrailRibbonGpu_Spawn(const TrailRibbonState *state,const TrailRibbonMaterial *material);
void TrailRibbonGpu_BeginUpdate(float dt,float time);
void TrailRibbonGpu_Update(int slot,float dt,const TrailRibbonAnchor *anchor);
void TrailRibbonGpu_EndUpdate(void);
void TrailRibbonGpu_Release(int slot,const TrailRibbonAnchor *anchor);
void TrailRibbonGpu_Kill(int slot);
void TrailRibbonGpu_Unload(void);
/* Direct GPU strip: straight-alpha color/texture, with world-space width.
 * The manager must reject/fallback for unsupported appearance features. */
void TrailRibbonGpu_Draw(int slot,Camera3D camera,float width,Color color,Texture2D texture);
#endif
