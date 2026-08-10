// ── COMPOSITE. VFX_ComposeProjectile — the owner's reference bolt ───────────
//
// A SCORE over primaries and nothing else, which is what a composite is supposed
// to be (VFX_PLAN §Part 4). Its whole content is *which primaries, where, at
// what strength*. There is no new visual idea in this file, and if one appears
// here later it belongs in a primary instead.
//
// The structure is the owner's, 29/07:
//
//     quả cầu  ── 1 trường năng lượng nhỏ, cạnh gắn vào đầu tròn,
//                 đỉnh nhọn phía sau
//              ── 1 đuôi chính
//              ── 2 đuôi phụ, chuyển động xoắn spiral, một đầu đính vào quả cầu
//
//   1. THE ORB      `VFX_ComposeEnergyOrb` — fresnel shell + hot core.
//   2. THE FIELD    `VFX_ComposeVolumeTrail` — small, faint tube behind the tail.
//   3. THE MAIN TAIL `TRAIL_PRESET_MAIN` — defined and textured. The shape.
//   4. TWO WISPS    `TRAIL_PRESET_WISP`, bent into a render helix around the
//                   flight axis; their emitters orbit only inside the hot core.
//
// WHY THE WISPS NEED THIS FILE AT ALL, when everything else is a plain call:
// their render helix needs the flight axis, which only exists here — it comes
// from where the projectile has been, not from anything the caller passes. The
// child transforms are owned by this pool and stay inside the head, so their
// ADDRESSES must remain stable for the trails' whole lives.
//
// ONE-SHOT + POOLED. Call once when the projectile spawns, keep the handle,
// release it on impact. Calling it every frame stacks projectiles until the pool
// recycles.

#include "core/tuning.h"

#define PROJ_MAX 6 // concurrent projectiles
#define PROJ_WISPS 2

// Per-wisp: width x radius, tail memory in seconds, spiral radius x radius,
// starting phase in radians, and a multiplier on the turn rate.
//
// Nothing here is uniform on purpose. Two wisps of equal length spiralling at
// the same rate 180 degrees apart draw a perfect double helix, which reads as a
// machined part rather than as loose energy — and because they stay exactly
// opposite, they cross the silhouette at the same instants and the bolt pulses
// in a way nothing physical does. Different turn rates are what makes them drift
// in and out of phase on their own, so the pattern never repeats.
static const float k_wispWidth[PROJ_WISPS] = {0.95f, 0.68f};
static const float k_wispLife[PROJ_WISPS] = {0.88f, 0.61f};
static const float k_wispRadius[PROJ_WISPS] = {0.62f, 0.88f};
static const float k_wispPhase[PROJ_WISPS] = {0.00f, 2.10f}; // ~120 deg, not 180
static const float k_wispRate[PROJ_WISPS] = {1.00f, 0.73f};

static float s_projSpiralTurns = 1.35f; // revolutions per second
// Radius relative to the orb. This bends the already-laid trail, never the
// emitter itself, so 0.5–0.8 stays visibly wide but begins at the core.
static float s_projSpiralR = 0.65f;
// A tiny real orbit makes newly-laid nodes inherit live motion. It is separate
// from the visible helix radius and remains fully inside the hot core.
static float s_projSpiralHeadR = 0.14f;
static float s_projFieldLen = 0.82f;    // x on the field's memory
static float s_projScale = 1.0f;

typedef struct
{
    bool active;
    const Matrix *xf; // caller-owned; must outlive the handle
    VC_MaterialId matId;
    float radius;
    // The child transforms the trails follow. Their ADDRESSES are handed to the
    // trail system, so they must not move — hence in the slot, not on the stack.
    Matrix headXf;
    Matrix wispXf[PROJ_WISPS];
    int fieldH, mainH, wispH[PROJ_WISPS];
    Vector3 prevHead;
    bool hasPrev;
    Vector3 axis; // smoothed direction of travel — the spiral's axis
    bool hasAxis;
    float phase;
} VC_Projectile;

static VC_Projectile s_proj[PROJ_MAX];
static bool s_projInit = false;

static void Proj_InitShared(void)
{
    if (s_projInit)
        return;
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("proj_spiral_turns", &s_projSpiralTurns, 1.35f);
    Tuning_RegisterFloat("proj_spiral_r", &s_projSpiralR, 0.65f);
    Tuning_RegisterFloat("proj_spiral_head_r", &s_projSpiralHeadR, 0.14f);
    Tuning_RegisterFloat("proj_field_len", &s_projFieldLen, 0.82f);
    Tuning_RegisterFloat("proj_scale", &s_projScale, 1.0f);
    s_projInit = true;
}

int VFX_ComposeProjectile(const Matrix *followTransform, VC_MaterialId mat,
                          float radius)
{
    Proj_InitShared();
    if (!followTransform)
    {
        TraceLog(LOG_WARNING, "VFX_PROJ: NULL transform — no projectile created");
        return -1;
    }
    if (radius <= 0.0f)
        radius = 0.35f;

    int slot = -1;
    for (int i = 0; i < PROJ_MAX; i++)
        if (!s_proj[i].active)
        {
            slot = i;
            break;
        }
    if (slot < 0)
    {
        // Announced: a projectile that never appears and one that was never
        // requested look identical on screen (core/CLAUDE.md §4).
        TraceLog(LOG_WARNING, "VFX_PROJ: pool full (%d) — no projectile created", PROJ_MAX);
        return -1;
    }

    VC_Projectile *p = &s_proj[slot];
    Vector3 head = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *followTransform);

    p->active = true;
    p->xf = followTransform;
    p->matId = mat;
    p->radius = radius * s_projScale;
    p->headXf = MatrixTranslate(head.x, head.y, head.z);
    p->prevHead = head;
    p->hasPrev = true;
    p->axis = (Vector3){0.0f, 0.0f, 1.0f};
    p->hasAxis = false;
    p->phase = 0.0f;
    for (int w = 0; w < PROJ_WISPS; w++)
        p->wispXf[w] = p->headXf;

    float r = p->radius;
    // THE FIELD stays deliberately smaller than the main ribbon. It is only a
    // soft wake mass, never a second large projectile around the real one.
    p->fieldH = VFX_ComposeVolumeTrail(&p->headXf, mat, r * 0.78f,
                                       0.48f * s_projFieldLen, VOL_ENERGY);

    p->mainH = VFX_ComposeTrail(&p->headXf, mat, r * 2.6f, 0.75f,
                                      TRAIL_PRESET_MAIN);
    // THE WISPS ARE NOT TWINS. Equal length, equal width and a fixed 180-degree
    // phase offset make two threads that mirror each other exactly, and a mirror
    // pair reads as a mechanism — the guide's "too straight and uniform". Every
    // property below is deliberately unequal, and the phases are NOT evenly
    // spaced around the circle for the same reason.
    for (int w = 0; w < PROJ_WISPS; w++)
    {
        p->wispH[w] = VFX_ComposeTrail(&p->wispXf[w], mat,
                                             r * k_wispWidth[w], k_wispLife[w],
                                             TRAIL_PRESET_WISP);
        SweptTrail_SetAnchoredHelix(p->wispH[w], p->axis,
                                    r * s_projSpiralR * k_wispRadius[w],
                                    1.10f * k_wispRate[w], k_wispPhase[w]);
    }
    return slot;
}

void VFX_KillProjectile(int handle)
{
    if (handle < 0 || handle >= PROJ_MAX)
        return;
    VC_Projectile *p = &s_proj[handle];
    if (!p->active)
        return;
    // Kill, not cut. Each trail stops being fed and drains its own history, which
    // is the wind-down — and killing DETACHES, so nothing holds this slot's
    // matrices after the slot is reused.
    VFX_KillVolumeTrail(p->fieldH);
    VFX_KillTrail(p->mainH);
    for (int w = 0; w < PROJ_WISPS; w++)
        VFX_KillTrail(p->wispH[w]);
    p->active = false;
    p->xf = NULL;
}

static void VC_Projectile_Update(float dt)
{
    if (dt <= 0.0f)
        return;
    for (int i = 0; i < PROJ_MAX; i++)
    {
        VC_Projectile *p = &s_proj[i];
        if (!p->active || !p->xf)
            continue;

        Vector3 head = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *p->xf);
        p->headXf = MatrixTranslate(head.x, head.y, head.z);

        // THE AXIS OF TRAVEL, smoothed. Taken raw it jitters whenever the
        // projectile is momentarily still, and the wisps' spiral basis flips with
        // it — so the two threads snap across the bolt in one frame. Smoothing is
        // cheaper than special-casing the still frames, and a projectile's
        // heading genuinely does not change instantly.
        Vector3 step = Vector3Subtract(head, p->prevHead);
        if (Vector3LengthSqr(step) > 1e-8f)
        {
            Vector3 dir = Vector3Normalize(step);
            p->axis = p->hasAxis ? Vector3Normalize(Vector3Lerp(p->axis, dir, 0.25f))
                                 : dir;
            p->hasAxis = true;
        }
        p->prevHead = head;

        p->phase += dt * s_projSpiralTurns * 2.0f * PI;
        for (int w = 0; w < PROJ_WISPS; w++)
        {
            // Its own rate and radius keep the pair organic. The emitter gets
            // only a tiny physical orbit, safely inside the core; that records
            // local motion in history without visually detaching from the source.
            Vector3 ref = (fabsf(p->axis.y) > 0.9f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                                    : (Vector3){0.0f, 1.0f, 0.0f};
            Vector3 u = Vector3Normalize(Vector3CrossProduct(p->axis, ref));
            Vector3 v = Vector3CrossProduct(p->axis, u);
            float ph = p->phase * k_wispRate[w] + k_wispPhase[w];
            float rad = p->radius * s_projSpiralR * k_wispRadius[w];
            float headRad = p->radius * s_projSpiralHeadR * k_wispRadius[w];
            Vector3 headOffset = Vector3Add(Vector3Scale(u, cosf(ph) * headRad),
                                            Vector3Scale(v, sinf(ph) * headRad));
            Vector3 emitter = Vector3Add(head, headOffset);
            p->wispXf[w] = MatrixTranslate(emitter.x, emitter.y, emitter.z);
            SweptTrail_SetAnchoredHelix(p->wispH[w], p->axis, rad,
                                        1.10f * k_wispRate[w], ph * 0.16f);
        }
    }
}

static void VC_Projectile_Draw3D(Camera3D cam)
{
    (void)cam;
    // The trails are drawn by the trail system. The only thing this composite
    // draws itself is the orb, because it is immediate-mode geometry rather than
    // a pooled entity.
    for (int i = 0; i < PROJ_MAX; i++)
    {
        VC_Projectile *p = &s_proj[i];
        if (!p->active || !p->xf)
            continue;
        Vector3 head = Vector3Transform((Vector3){0.0f, 0.0f, 0.0f}, *p->xf);
        // No shell: a small hot point marks the source without competing with
        // the trails' silhouette.
        VFX_ComposeCoreGlow(head, p->matId, p->radius * 0.78f, 0.90f);
    }
}
