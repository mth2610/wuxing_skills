#ifndef CORE_TRAIL_RIBBON_SOLVER_H
#define CORE_TRAIL_RIBBON_SOLVER_H

#include "core/motion/motion_body.h"
#include <string.h>

#define TRAIL_RIBBON_MAX_NODES 60
#define TRAIL_RIBBON_FIXED_DT (1.0f / 120.0f)
#define TRAIL_RIBBON_INV_FIXED_DT 120.0f
#define TRAIL_RIBBON_INV_FIXED_DT_SQ 14400.0f
#define TRAIL_RIBBON_MAX_STEPS 8
#define TRAIL_RIBBON_MAX_ITERATIONS 32

typedef enum { TRAIL_RIBBON_FREE = 0, TRAIL_RIBBON_HEAD_ANCHORED = 1 } TrailRibbonMode;
typedef enum { TRAIL_RIBBON_AUTO = 0, TRAIL_RIBBON_CPU_ONLY = 1, TRAIL_RIBBON_GPU_ONLY = 2 } TrailRibbonBackend;

/* A frame snapshot, not a pointer into caller-owned transform storage.
 * discontinuity rebases the whole chain by the head displacement. */
typedef struct {
    Vector3 previousPosition, position, velocity;
    bool valid, discontinuity;
} TrailRibbonAnchor;

typedef struct {
    MotionBodyProfile body;
    unsigned int receiverMask; /* 0 selects the trail receiver channel. */
    int constraintIterations;
    float stretchCompliance; /* m/N; zero is inextensible. */
    float bendCompliance;    /* m/N; negative disables second-neighbor bending. */
} TrailRibbonMaterial;

/* Head-first, fixed topology. previous is substep scratch, not render history.
 * restLength[i] connects i-1 to i (element 0 is unused). */
typedef struct {
    Vector3 position[TRAIL_RIBBON_MAX_NODES];
    Vector3 velocity[TRAIL_RIBBON_MAX_NODES];
    Vector3 previous[TRAIL_RIBBON_MAX_NODES];
    float restLength[TRAIL_RIBBON_MAX_NODES];
    int count;
    TrailRibbonMode mode;
    float accumulator;
} TrailRibbonState;

typedef void (*TrailRibbonSampleFn)(void *user, Vector3 position, Vector3 velocity,
    float dt, int node, float timeOffset, FieldSample *sample, Vector3 *ordinaryAir);

static inline TrailRibbonMaterial TrailRibbonMaterial_Default(void) {
    TrailRibbonMaterial m = {0};
    m.body.inverseMassKg = 250.0f;
    m.body.gravityScale = 1.0f;
    m.constraintIterations = 16;
    m.bendCompliance = -1.0f;
    return m;
}

static inline bool TrailRibbon_Initialize(TrailRibbonState *s, int count,
    Vector3 head, Vector3 tailDirection, float length, Vector3 velocity, TrailRibbonMode mode) {
    if (!s || count < 2 || count > TRAIL_RIBBON_MAX_NODES || !isfinite(length) || length <= 0.0f ||
        !Field_FiniteVector(head) || !Field_FiniteVector(tailDirection) ||
        !Field_FiniteVector(velocity) || MotionVec_Length(tailDirection) < 1e-6f ||
        (mode != TRAIL_RIBBON_FREE && mode != TRAIL_RIBBON_HEAD_ANCHORED))
        return false;
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->mode = mode;
    float segLen = length / (float)(count - 1);
    Vector3 step = MotionVec_Scale(MotionVec_Normalize(tailDirection), segLen);
    for (int i = 0; i < count; i++) {
        s->position[i] = MotionVec_Add(head, MotionVec_Scale(step, (float)i));
        s->previous[i] = s->position[i];
        s->velocity[i] = velocity;
        s->restLength[i] = i ? segLen : 0.0f;
    }
    return true;
}

static inline void TrailRibbon_Release(TrailRibbonState *s, const TrailRibbonAnchor *anchor) {
    if (!s || s->mode != TRAIL_RIBBON_HEAD_ANCHORED) return;
    if (anchor && anchor->valid) s->velocity[0] = anchor->velocity;
    s->mode = TRAIL_RIBBON_FREE;
}

/* Precomputed XPBD denominators eliminate ~3,700 float divisions per substep. */
static inline void TrailRibbon_ProjectPairFast(TrailRibbonState *s, int a, int b, float rest,
    float alpha, float inverseMass, float invDenomFree, float invDenomPinned, bool pinned, float *lambda) {
    Vector3 delta = MotionVec_Sub(s->position[b], s->position[a]);
    float lenSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    float length;
    if (lenSq < 1e-16f) {
        delta = MotionVec_Sub(s->previous[b], s->previous[a]);
        float prevLenSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (prevLenSq < 1e-16f) {
            delta = (Vector3){0.0f, -1.0f, 0.0f};
        } else {
            delta = MotionVec_Scale(delta, 1.0f / sqrtf(prevLenSq));
        }
        length = 0.0f;
    } else {
        length = sqrtf(lenSq);
        delta = MotionVec_Scale(delta, 1.0f / length);
    }
    bool pinA = (pinned && a == 0);
    float wa = pinA ? 0.0f : inverseMass;
    float invDenom = pinA ? invDenomPinned : invDenomFree;
    float change = (-(length - rest) - alpha * (*lambda)) * invDenom;
    *lambda += change;
    s->position[a] = MotionVec_Sub(s->position[a], MotionVec_Scale(delta, wa * change));
    s->position[b] = MotionVec_Add(s->position[b], MotionVec_Scale(delta, inverseMass * change));
}

static inline void TrailRibbon_ProjectPair(TrailRibbonState *s, int a, int b, float rest,
    float compliance, float inverseMass, bool pinned, float *lambda) {
    float alpha = fmaxf(compliance, 0.0f) * TRAIL_RIBBON_INV_FIXED_DT_SQ;
    float invDenomFree = 1.0f / fmaxf(2.0f * inverseMass + alpha, 1e-12f);
    float invDenomPinned = 1.0f / fmaxf(inverseMass + alpha, 1e-12f);
    TrailRibbon_ProjectPairFast(s, a, b, rest, alpha, inverseMass, invDenomFree, invDenomPinned, pinned, lambda);
}

/* External movement comes exclusively from MotionBody. XPBD only enforces
 * intrinsic connected geometry; it never invents an orbit or field. */
static inline void TrailRibbon_Step(TrailRibbonState *s, const TrailRibbonMaterial *m,
    const TrailRibbonAnchor *anchor, TrailRibbonSampleFn sampleFn, void *user, float timeOffset) {
    const float dt = TRAIL_RIBBON_FIXED_DT;
    bool pinned = (s->mode == TRAIL_RIBBON_HEAD_ANCHORED && anchor && anchor->valid);
    if (s->mode == TRAIL_RIBBON_HEAD_ANCHORED && !pinned) TrailRibbon_Release(s, NULL);
    float lambda[TRAIL_RIBBON_MAX_NODES] = {0}, bendLambda[TRAIL_RIBBON_MAX_NODES] = {0};
    for (int i = 0; i < s->count; i++) {
        s->previous[i] = s->position[i];
        if (i == 0 && pinned) {
            s->position[0] = anchor->position;
            continue;
        }
        FieldSample sample = {0};
        Vector3 air = {0};
        if (sampleFn) sampleFn(user, s->position[i], s->velocity[i], dt, i, timeOffset, &sample, &air);
        s->velocity[i] = MotionBody_AdvanceFieldVelocity(s->velocity[i], &m->body,
            (Vector3){0}, (Vector3){0}, &sample, air, dt);
        s->position[i] = MotionVec_Add(s->position[i], MotionVec_Scale(s->velocity[i], dt));
    }
    int iterations = m->constraintIterations;
    if (iterations < 1) iterations = 1;
    if (iterations > TRAIL_RIBBON_MAX_ITERATIONS) iterations = TRAIL_RIBBON_MAX_ITERATIONS;
    float invMass = (m->body.inverseMassKg > 0.0f) ? m->body.inverseMassKg : 1.0f;

    float alphaStretch = fmaxf(m->stretchCompliance, 0.0f) * TRAIL_RIBBON_INV_FIXED_DT_SQ;
    float invDenomFreeStretch = 1.0f / fmaxf(2.0f * invMass + alphaStretch, 1e-12f);
    float invDenomPinnedStretch = 1.0f / fmaxf(invMass + alphaStretch, 1e-12f);

    float alphaBend = fmaxf(m->bendCompliance, 0.0f) * TRAIL_RIBBON_INV_FIXED_DT_SQ;
    float invDenomFreeBend = 1.0f / fmaxf(2.0f * invMass + alphaBend, 1e-12f);
    float invDenomPinnedBend = 1.0f / fmaxf(invMass + alphaBend, 1e-12f);

    for (int pass = 0; pass < iterations; pass++) {
        if (m->bendCompliance >= 0.0f) {
            for (int i = 2; i < s->count; i++) {
                TrailRibbon_ProjectPairFast(s, i - 2, i, s->restLength[i - 1] + s->restLength[i],
                    alphaBend, invMass, invDenomFreeBend, invDenomPinnedBend, pinned, &bendLambda[i]);
            }
        }
        /* Alternate sweeps reduce directional bias without moving the pin. */
        for (int k = 1; k < s->count; k++) {
            int i = (pass & 1) ? (s->count - k) : k;
            TrailRibbon_ProjectPairFast(s, i - 1, i, s->restLength[i],
                alphaStretch, invMass, invDenomFreeStretch, invDenomPinnedStretch, pinned, &lambda[i]);
        }
    }
    for (int i = 0; i < s->count; i++) {
        s->velocity[i] = MotionVec_Scale(MotionVec_Sub(s->position[i], s->previous[i]), TRAIL_RIBBON_INV_FIXED_DT);
    }
    if (pinned) s->velocity[0] = anchor->velocity;
}

/* At most eight fixed steps per call. Excess hitch debt is discarded rather
 * than silently integrating a large unstable dt. Caller still ages lifetime. */
static inline int TrailRibbon_Advance(TrailRibbonState *s, const TrailRibbonMaterial *m,
    float dt, const TrailRibbonAnchor *anchor, TrailRibbonSampleFn sampleFn, void *user) {
    if (!s || !m || !isfinite(dt) || dt <= 0.0f || s->count < 2) return 0;
    if (s->mode == TRAIL_RIBBON_HEAD_ANCHORED && (!anchor || !anchor->valid))
        TrailRibbon_Release(s, NULL);
    if (anchor && anchor->valid && anchor->discontinuity && s->mode == TRAIL_RIBBON_HEAD_ANCHORED) {
        Vector3 jump = MotionVec_Sub(anchor->position, s->position[0]);
        for (int i = 0; i < s->count; i++) {
            s->position[i] = MotionVec_Add(s->position[i], jump);
            s->previous[i] = s->position[i];
        }
        s->velocity[0] = anchor->velocity;
    }
    s->accumulator += fminf(dt, TRAIL_RIBBON_FIXED_DT * TRAIL_RIBBON_MAX_STEPS);
    int steps = (int)((s->accumulator + 1e-7f) * TRAIL_RIBBON_INV_FIXED_DT);
    if (steps > TRAIL_RIBBON_MAX_STEPS) steps = TRAIL_RIBBON_MAX_STEPS;
    Vector3 start = s->position[0];
    float invSteps = (steps > 0) ? (1.0f / (float)steps) : 0.0f;
    Vector3 deltaAnchor = anchor ? MotionVec_Sub(anchor->position, start) : (Vector3){0};
    for (int k = 0; k < steps; k++) {
        TrailRibbonAnchor sub = {0};
        if (anchor) {
            sub = *anchor;
            sub.position = MotionVec_Add(start, MotionVec_Scale(deltaAnchor, (float)(k + 1) * invSteps));
        }
        TrailRibbon_Step(s, m, anchor ? &sub : NULL, sampleFn, user, -(float)(steps - k) * TRAIL_RIBBON_FIXED_DT);
    }
    s->accumulator = fmaxf(0.0f, s->accumulator - (float)steps * TRAIL_RIBBON_FIXED_DT);
    return steps;
}
#endif
