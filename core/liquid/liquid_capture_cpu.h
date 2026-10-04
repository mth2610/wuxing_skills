#ifndef CORE_LIQUID_CAPTURE_CPU_H
#define CORE_LIQUID_CAPTURE_CPU_H

#include "raylib.h"
#include <math.h>
#include <stdbool.h>

/* Internal SSF capture helper. The caller owns the front/back targets, depth
 * test, blending and viewport. Both draws reuse one prepared vertex upload. */
#define LIQUID_CAPTURE_CPU_MAX_INSTANCES 384

typedef struct LiquidCaptureCPUInstance {
    Vector3 position;
    Vector3 radii;                 /* world-axis ellipsoid semiaxes, metres */
    int material;
} LiquidCaptureCPUInstance;

bool LiquidCaptureCPU_Init(void);
void LiquidCaptureCPU_Unload(void);
/* Returns the number of valid, screen-intersecting instances prepared. */
int LiquidCaptureCPU_Prepare(const LiquidCaptureCPUInstance *instances, int count,
                             Matrix view, Matrix projection);
void LiquidCaptureCPU_Draw(bool backDepth);
/* Optional front winner mask for material-consistent back envelopes. */
void LiquidCaptureCPU_SetFrontDepth(Texture2D frontDepth);
int LiquidCaptureCPU_GetPreparedCount(void);

/* Pure capture arithmetic kept here so headless tests exercise the production
 * bounds calculation. Ray roots use the same closest-approach formulation as
 * liquid_capture_ellipsoid.fs; these tests do not execute that GLSL stage. */
static inline Vector4 LiquidCaptureCPU_Transform4(Matrix m, Vector4 v)
{
    return (Vector4){m.m0*v.x + m.m4*v.y + m.m8*v.z + m.m12*v.w,
                     m.m1*v.x + m.m5*v.y + m.m9*v.z + m.m13*v.w,
                     m.m2*v.x + m.m6*v.y + m.m10*v.z + m.m14*v.w,
                     m.m3*v.x + m.m7*v.y + m.m11*v.z + m.m15*v.w};
}

static inline bool LiquidCaptureCPU_UnprojectNDC(Matrix inverseProjection,
                                                 Vector3 ndc, Vector3 *outView)
{
    if (!outView) return false;
    Vector4 point = LiquidCaptureCPU_Transform4(inverseProjection,
                                                (Vector4){ndc.x,ndc.y,ndc.z,1});
    if (!isfinite(point.w) || fabsf(point.w) <= 1e-20f) return false;
    *outView = (Vector3){point.x/point.w,point.y/point.w,point.z/point.w};
    return isfinite(outView->x) && isfinite(outView->y) && isfinite(outView->z);
}

static inline bool LiquidCaptureCPU_ValidRadii(Vector3 radii)
{
    return isfinite(radii.x) && isfinite(radii.y) && isfinite(radii.z) &&
           radii.x > 0.0f && radii.y > 0.0f && radii.z > 0.0f;
}

static inline bool LiquidCaptureCPU_ProjectBounds(Vector3 center, Vector3 radii,
                                                   Matrix view, Matrix projection,
                                                   Vector4 *outBounds)
{
    if (!outBounds || !LiquidCaptureCPU_ValidRadii(radii) ||
        !isfinite(center.x) || !isfinite(center.y) || !isfinite(center.z))
        return false;
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    bool behindEye = false, inFrontOfEye = false;
    for (int corner = 0; corner < 8; ++corner) {
        Vector4 p = {center.x + ((corner&1) ? radii.x : -radii.x),
                     center.y + ((corner&2) ? radii.y : -radii.y),
                     center.z + ((corner&4) ? radii.z : -radii.z), 1.0f};
        p = LiquidCaptureCPU_Transform4(view, p);
        p = LiquidCaptureCPU_Transform4(projection, p);
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.w)) return false;
        if (p.w <= 1e-6f) { behindEye = true; continue; }
        inFrontOfEye = true;
        float x = p.x/p.w, y = p.y/p.w;
        minX = fminf(minX,x); minY = fminf(minY,y);
        maxX = fmaxf(maxX,x); maxY = fmaxf(maxY,y);
    }
    if (!inFrontOfEye) return false;
    /* Perspective projection is unbounded across the eye plane. Clipping the
     * centre billboard or projecting only its front corners can lose visible
     * ellipsoid roots, so rasterize the complete viewport in this rare case. */
    if (behindEye) { *outBounds = (Vector4){-1,-1,1,1}; return true; }
    if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) return false;
    /* Linear-fractional extrema over this positive-w world AABB occur at its
     * corners. The enclosed ellipsoid therefore cannot project outside it. */
    *outBounds = (Vector4){fmaxf(-1.0f,minX-1e-6f), fmaxf(-1.0f,minY-1e-6f),
                          fminf(1.0f,maxX+1e-6f), fminf(1.0f,maxY+1e-6f)};
    return outBounds->z > outBounds->x && outBounds->w > outBounds->y;
}

static inline bool LiquidCaptureCPU_RayEllipsoid(Vector3 origin, Vector3 direction,
                                                  Vector3 center, Vector3 radii,
                                                  float *nearRoot, float *farRoot)
{
    if (!nearRoot || !farRoot || !LiquidCaptureCPU_ValidRadii(radii)) return false;
    Vector3 o = {(origin.x-center.x)/radii.x, (origin.y-center.y)/radii.y,
                 (origin.z-center.z)/radii.z};
    Vector3 d = {direction.x/radii.x,direction.y/radii.y,direction.z/radii.z};
    float a = d.x*d.x + d.y*d.y + d.z*d.z;
    if (!isfinite(a) || a <= 1e-20f) return false;
    float closest = -(o.x*d.x + o.y*d.y + o.z*d.z)/a;
    Vector3 residual = {o.x+d.x*closest,o.y+d.y*closest,o.z+d.z*closest};
    float chordMetric = 1.0f - (residual.x*residual.x + residual.y*residual.y + residual.z*residual.z);
    if (!isfinite(chordMetric) || chordMetric < 0.0f) return false;
    float halfChord = sqrtf(chordMetric/a);
    *nearRoot = closest-halfChord;
    *farRoot = closest+halfChord;
    return isfinite(*nearRoot) && isfinite(*farRoot);
}

#endif
