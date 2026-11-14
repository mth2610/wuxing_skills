#ifndef CORE_MOTION_ANALYTIC_FLOW_H
#define CORE_MOTION_ANALYTIC_FLOW_H

#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANALYTIC_FLOW_MAX_SAMPLES 3200

typedef enum {
    ANALYTIC_FLOW_ORB = 0,          /* Ballistic teardrop swirl + impact burst */
    ANALYTIC_FLOW_DRAGON_SURGE = 1, /* Coiling serpentine dragon body + head swell */
    ANALYTIC_FLOW_BLADE_SLASH = 2,  /* 3-hit whipping crescent blade arcs */
    ANALYTIC_FLOW_VORTEX_SURGE = 3  /* Ascending helical vortex + radial expand */
} AnalyticFlowPattern;

typedef struct {
    AnalyticFlowPattern pattern;
    Vector3 origin;
    Vector3 target;
    float radius;      /* Base cross-section / orb radius in meters */
    float speed;       /* Travel speed in m/s (ORB / DRAGON_SURGE) */
    float arcHeight;   /* Peak arc height or vortex height in meters */
    float duration;    /* Total lifetime in seconds (<= 0.1 uses default) */
    int sampleCount;   /* Samples to evaluate, clamped to ANALYTIC_FLOW_MAX_SAMPLES */
} AnalyticFlowDesc;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float radius;
    float alpha;
    float u;
    bool active;
} AnalyticFlowSample;

/* Precompute low-discrepancy Halton(2,3,5) seed table (idempotent). */
void AnalyticFlow_InitSeeds(void);

/* Return precomputed Halton seed in [0, 1) for sample index and dim (0..2). */
float AnalyticFlow_HaltonSeed(int index, int dim);

/* Compute travel time (seconds) from origin to target at desc->speed. */
float AnalyticFlow_TravelTime(const AnalyticFlowDesc *desc);

/* Compute default total duration (seconds) for desc->pattern. */
float AnalyticFlow_DefaultDuration(const AnalyticFlowDesc *desc);

/* Evaluate deterministic O(N) analytic trajectory samples at `age` seconds.
 * Writes `count` entries into `outSamples` (up to `maxSamples`) and returns `count`.
 * Each entry has `active = true` when visible at `age`. */
int AnalyticFlow_Evaluate(const AnalyticFlowDesc *desc,
                          float age,
                          AnalyticFlowSample *outSamples,
                          int maxSamples);

/* Evaluate deterministic O(N) ballistic crown-splash burst around `center`.
 * Writes `count` entries into `outSamples` (up to `maxSamples`) and returns `count`. */
int AnalyticFlow_EvaluateCrownSplash(Vector3 center,
                                     float splashAge,
                                     float scale,
                                     int sampleCount,
                                     AnalyticFlowSample *outSamples,
                                     int maxSamples);

#ifdef __cplusplus
}
#endif

#endif /* CORE_MOTION_ANALYTIC_FLOW_H */
