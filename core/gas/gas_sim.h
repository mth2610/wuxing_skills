#ifndef WUXING_GAS_SIM_H
#define WUXING_GAS_SIM_H

#include <stdbool.h>

#define GAS_SIM_MAX_X 32
#define GAS_SIM_MAX_Y 32
#define GAS_SIM_MAX_Z 32
#define GAS_SIM_MAX_CELLS (GAS_SIM_MAX_X * GAS_SIM_MAX_Y * GAS_SIM_MAX_Z)

typedef struct GasSimVec3 {
    float x;
    float y;
    float z;
} GasSimVec3;

typedef struct GasSimInjection {
    GasSimVec3 position;  /* Normalized volume coordinates, [0, 1]. */
    float radius;         /* Normalized against the shortest grid axis. */
    GasSimVec3 velocity;  /* Grid cells per second. */
    float density;
    float temperature;
    float reaction;
} GasSimInjection;

typedef struct GasSimConfig {
    float buoyancy;
    float smokeWeight;
    float velocityDissipation;
    float densityDissipation;
    float temperatureDissipation;
    float reactionDissipation;
    int pressureIterations;
} GasSimConfig;

/* Fixed-capacity storage. Runtime dimensions may be smaller than the maximum. */
typedef struct GasSim {
    int width;
    int height;
    int depth;
    int cellCount;
    float velocityX[GAS_SIM_MAX_CELLS];
    float velocityY[GAS_SIM_MAX_CELLS];
    float velocityZ[GAS_SIM_MAX_CELLS];
    float velocityScratchX[GAS_SIM_MAX_CELLS];
    float velocityScratchY[GAS_SIM_MAX_CELLS];
    float velocityScratchZ[GAS_SIM_MAX_CELLS];
    float density[GAS_SIM_MAX_CELLS];
    float temperature[GAS_SIM_MAX_CELLS];
    float reaction[GAS_SIM_MAX_CELLS];
    float scalarScratchA[GAS_SIM_MAX_CELLS];
    float scalarScratchB[GAS_SIM_MAX_CELLS];
    float scalarScratchC[GAS_SIM_MAX_CELLS];
    float pressure[GAS_SIM_MAX_CELLS];
    float pressureScratch[GAS_SIM_MAX_CELLS];
    float divergence[GAS_SIM_MAX_CELLS];
} GasSim;

GasSimConfig GasSim_DefaultConfig(void);
bool GasSim_Init(GasSim *sim, int width, int height, int depth);
void GasSim_Clear(GasSim *sim);
int GasSim_GetCellCount(const GasSim *sim);
void GasSim_InjectSphere(GasSim *sim, GasSimInjection injection);
void GasSim_ProjectVelocity(GasSim *sim, int pressureIterations);
void GasSim_Step(GasSim *sim, float dt, const GasSimConfig *config);

float GasSim_GetTotalDensity(const GasSim *sim);
float GasSim_GetDensityCenterY(const GasSim *sim);
float GasSim_GetMeanAbsDivergence(const GasSim *sim);
float GasSim_GetMaxTemperature(const GasSim *sim);
float GasSim_GetMaxReaction(const GasSim *sim);
bool GasSim_IsFinite(const GasSim *sim);

#endif
