#ifndef CORE_TRAIL_RIBBON_H
#define CORE_TRAIL_RIBBON_H
#include "core/trails/trail_attachment.h"
#include "core/trails/trail_system.h"
#include <math.h>
#include "core/motion/motion_path_transport.h"

#define TRAIL_RIBBON_APPEARANCE_LAYERS 3
typedef struct {
    bool enabled;
    TrailMaterialConfig material;
    TrailDeformConfig deform;
    TrailLayer layers[TRAIL_RIBBON_APPEARANCE_LAYERS];
    int layerCount;
    TrailWidthEnvelopeType widthEnvelope;
    const SkillCurve *widthCurve, *alphaCurve;
    const ColorGradient *gradient;
    int ribbonMode;
    Vector3 fixedNormal;
    BlendMode blendMode;
} TrailRibbonAppearance;

/* Validate copied appearance data before allocating CPU/GPU resources. */
static inline bool TrailRibbonAppearance_IsValid(const TrailRibbonAppearance *a) {
    if(!a) return false;
    if(!a->enabled) return true;
    if(a->layerCount<0||a->layerCount>TRAIL_RIBBON_APPEARANCE_LAYERS||
       a->ribbonMode<RIBBON_CAMERA_FACING||a->ribbonMode>RIBBON_FIXED_NORMAL||
       (a->blendMode!=BLEND_ALPHA&&a->blendMode!=BLEND_ADDITIVE&&a->blendMode!=BLEND_ALPHA_PREMULTIPLY)||
       !isfinite(a->fixedNormal.x)||!isfinite(a->fixedNormal.y)||!isfinite(a->fixedNormal.z)||
       !isfinite(a->material.mode)||!isfinite(a->material.bodyOpacity)||!isfinite(a->material.hdrGain)||
       !isfinite(a->deform.phase)||!isfinite(a->deform.envHead)||!isfinite(a->deform.envTail)) return false;
    for(int i=0;i<a->layerCount;i++) {
        const TrailLayer *l=&a->layers[i];
        if(!isfinite(l->widthMul)||!isfinite(l->alphaMul)||!isfinite(l->whiten)||
           !isfinite(l->headAlphaPow)||!isfinite(l->scrollMul)) return false;
    }
    return true;
}

/* Modern opt-in chain API. It shares TrailSystem lifetime/kill ownership.
 * Plain alpha and shared TrailRecipe appearance run on both backends.
 * Appearance never displaces world-space nodes; Motion owns their movement.
 * Legacy TrailConfig continues through its original renderer. */
typedef struct {
    TrailRibbonMode mode;
    TrailRibbonBackend backend;
    TrailRibbonMaterial material;
    int nodeCount;
    Vector3 headPosition, tailDirection, initialVelocity;
    float lengthM, widthM, lifetimeSec;
    Color color;
    Texture2D texture;
    /* Copied at spawn. Zero/disabled preserves the plain fast renderer.
     * Appearance curves/gradients/textures retain their normal borrowed lifetime. */
    TrailRibbonAppearance appearance;
    TrailAttachmentHandle attachment;
    Vector3 attachmentOffset;
    MotionPathTransport pathTransport; /* Opt-in prescribed Motion; zero retains free-body dynamics. */
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
void TrailRibbonSystem_EndUpdate(void);
bool TrailRibbonSystem_Update(int trailId,float dt);
void TrailRibbonSystem_Kill(int trailId);
void TrailRibbonSystem_Draw(Camera3D camera,int layerFilter);
bool TrailRibbonSystem_IsModern(int trailId);
void TrailRibbonSystem_Unload(void);
/* Internal shared renderer binding; no Motion or geometry displacement. */
void TrailRibbon_BindAppearance(Shader shader,const TrailEntity *trail,Camera3D camera,int layerFilter);
float TrailRibbon_WidthEnvelope(const TrailEntity *trail,float headRatio,float time);
float TrailRibbon_LayerAlpha(const TrailEntity *trail,const TrailLayer *layer,int layerFilter);
#endif
