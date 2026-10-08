#ifndef CORE_TRAIL_RIBBON_H
#define CORE_TRAIL_RIBBON_H
#include "core/trails/trail_attachment.h"

/* Modern opt-in chain API. It shares TrailSystem lifetime/kill ownership.
 * Plain alpha textured ribbon appearance is currently supported on both
 * backends. Legacy TrailConfig continues through its original renderer. */
typedef struct {
    TrailRibbonMode mode;
    TrailRibbonBackend backend;
    TrailRibbonMaterial material;
    int nodeCount;
    Vector3 headPosition, tailDirection, initialVelocity;
    float lengthM, widthM, lifetimeSec;
    Color color;
    Texture2D texture;
    TrailAttachmentHandle attachment;
    Vector3 attachmentOffset;
} TrailRibbonConfig;
TrailRibbonConfig TrailRibbon_Default(void);
/* Returns a TrailSystem id usable with KillTrail; -1 rejects invalid config,
 * pool exhaustion, or GPU_ONLY on an unavailable device. */
int TrailRibbon_Spawn(const TrailRibbonConfig *config);
bool TrailRibbon_ReleaseHead(int trailId);
/* NULL for GPU-resident or non-modern trails; never reads GPU state back. */
const TrailRibbonState *TrailRibbon_GetState(int trailId);
TrailRibbonBackend TrailRibbon_GetBackend(int trailId);
TrailAttachmentHandle TrailAttachment_Create(Matrix transform);
bool TrailAttachment_Update(TrailAttachmentHandle handle,Matrix transform,float dt,bool discontinuity);
void TrailAttachment_Destroy(TrailAttachmentHandle handle);

/* Manager hooks: these are called by TrailSystem, not application update code. */
void TrailRibbonSystem_Reset(void);
void TrailRibbonSystem_BeginUpdate(float dt,float time);
bool TrailRibbonSystem_Update(int trailId,float dt);
void TrailRibbonSystem_Kill(int trailId);
void TrailRibbonSystem_Draw(Camera3D camera,int layerFilter);
bool TrailRibbonSystem_IsModern(int trailId);
void TrailRibbonSystem_Unload(void);
#endif
