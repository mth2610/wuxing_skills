#ifndef CORE_LIQUID_FLOW_H
#define CORE_LIQUID_FLOW_H

#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIQUID_FLOW_ORB = 0,         /* Cohesive swirling water orb -> ballistic arc -> crown splash */
    LIQUID_FLOW_DRAGON_SURGE,    /* Coiling serpentine water surge -> head swell -> crown splash */
    LIQUID_FLOW_BLADE_SLASH,     /* 3-hit whipping crescent liquid blade slashes */
    LIQUID_FLOW_VORTEX_SURGE     /* Ascending helical water vortex & radial scatter */
} LiquidFlowPattern;

typedef struct {
    LiquidFlowPattern pattern;
    Vector3 origin;
    Vector3 target;
    float radius;       /* Characteristic radius in meters (default 0.42f) */
    float speed;        /* Travel speed m/s (default 14.0f) */
    float arcHeight;    /* Parabolic apex lift in meters (default 1.8f) */
    float duration;     /* Total duration in seconds (0 = auto from pattern) */
    int parcelCount;    /* Number of cohesive SSF parcels (default 220, max 320) */
    Color bodyColor;    /* {0} uses unified song_quao river jade-teal */
    Color glowColor;    /* {0} uses unified river scatter */
    Color softColor;    /* {0} uses unified river foam/crest */
    bool sampleTerrain; /* Clamp splash/pool height to MapManager ground */
} LiquidFlowDesc;

void LiquidFlow_Init(void);
void LiquidFlow_Unload(void);

/* Spawns an O(N) analytic fluid-parcel flow (inspired by water_vfx.html).
 * Returns handle >= 0, or -1 if pool is full. */
int LiquidFlow_Spawn(const LiquidFlowDesc *desc);

void LiquidFlow_Update(float dt);
void LiquidFlow_DrawSSF(void);
void LiquidFlow_DrawSmartShadows(void);
void LiquidFlow_DrawShadowPass(void);
int LiquidFlow_GetActiveCount(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_LIQUID_FLOW_H */
