#ifndef CORE_LIQUID_SWE_H
#define CORE_LIQUID_SWE_H

#include "raylib.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIQUID_SWE_MAX_INSTANCES    6
#define LIQUID_SWE_GRID_NX          68
#define LIQUID_SWE_GRID_NZ          68
#define LIQUID_SWE_MAX_EMITTERS     20
#define LIQUID_SWE_MAX_MANIFOLDS    3
#define LIQUID_SWE_MANIFOLD_U_STEPS 36
#define LIQUID_SWE_MANIFOLD_V_STEPS 20

typedef enum {
    LIQUID_SWE_SHAPE_SPHERE = 0,        /* Pressurized levitating water orb (Avatar State) */
    LIQUID_SWE_SHAPE_TAI_CHI_WHIP = 1,  /* Coiling Tai-Chi spiral -> snapping S-curve water whip */
    LIQUID_SWE_SHAPE_TORUS_CHAKRAM = 2, /* Tilted 3D orbital torus ring / razor water chakram */
    LIQUID_SWE_SHAPE_WATER_SPOUT = 3,   /* Towering helical water cyclone funnel */
    LIQUID_SWE_SHAPE_OCTOPUS_FORM = 4,  /* 8 articulated radial water tentacles */
    LIQUID_SWE_SHAPE_CURLING_WAVE = 5,  /* Overhanging concave curling tidal wave wall */
    LIQUID_SWE_SHAPE_TWIN_WHIP = 6,     /* C-infinity twin horizontal spinning water whips */
    LIQUID_SWE_SHAPE_COUNT
} LiquidSWEShapeType;

typedef enum {
    LIQUID_SWE_PATTERN_TIDAL_WAVE = 0,      /* Classic curling crescent tsunami bore */
    LIQUID_SWE_PATTERN_RADIAL_DELUGE = 1,   /* 360-degree expanding flood ring */
    LIQUID_SWE_PATTERN_TWIN_TIDAL = 2,      /* Twin converging side waves */
    LIQUID_SWE_PATTERN_TAI_CHI_WHIP = 3,    /* Skill 1: Thai Cuc Thuy Tien */
    LIQUID_SWE_PATTERN_ORBITAL_CHAKRAM = 4, /* Skill 2: Thien Khong Thuy Hoan */
    LIQUID_SWE_PATTERN_WATER_SPOUT = 5,     /* Skill 3: Ton Su Thuy Long Quyen */
    LIQUID_SWE_PATTERN_OCTOPUS_FORM = 6,    /* Skill 4: Bat Xuc Thuy Hinh */
    LIQUID_SWE_PATTERN_CURLING_WAVE = 7,    /* Skill 5: Cuong Dao Quyen Vu */
    LIQUID_SWE_PATTERN_TRIPLE_DISCS = 8,    /* Skill 6: Lien Hoan Thuy Luan */
    LIQUID_SWE_PATTERN_TWIN_WHIPS = 9,      /* Skill 7: Song Tien Toan Phong */
    LIQUID_SWE_PATTERN_AVATAR_STATE = 10,   /* Skill 8: Thai Cuc Avatar State */
    LIQUID_SWE_PATTERN_GLACIAL_SURGE = 11,  /* Cryo 1: Surging wave flash-freezes into glacial wall */
    LIQUID_SWE_PATTERN_FROZEN_OCTOPUS = 12, /* Cryo 2: 8 water tentacles freeze into icy cage */
    LIQUID_SWE_PATTERN_COUNT
} LiquidSWEPattern;

typedef struct {
    Vector2 posXZ;
    float radius;
    float heightRate;    /* >0 injects water/crest, <0 drains/suctions puddle */
    Vector2 linearVel;
    float swirl;         /* Tangential vortex torque */
    float radialImpulse; /* Radial shockwave push (+) or pull (-) */
} LiquidSWEEmitter;

typedef struct {
    bool active;
    LiquidSWEShapeType shape;
    Vector3 center;
    float captureRadius;
    float detachAlpha;
    float sizeScale3D;
    float spinPhase;
    float progressTau;
    Vector2 origXZ;
    Vector2 targetXZ;
    float archHeight;
    float lateralCurve;
    float swaySign;
} LiquidSWEManifold;

typedef struct {
    LiquidSWEPattern pattern;
    Vector3 source;
    Vector3 target;
    float waveHeight;
    float waveSpeed;
    float width;
    float length;
    float duration;
    float curlIntensity;
    float drag;
    float gravity;
    float evaporation;
    float obstacleScale;
    float skillPower;
    int renderMode;      /* 0: AAA Hydro-Combat, 1: Velocity & Foam, 2: Height + Bathymetry */
    bool emitCrestSpray;
    bool emitWetGround;
    bool sampleTerrain;
    Color deepColor;
    Color crestColor;
    Color foamColor;
    float cryoMode;      /* 0.0 = pure liquid, 1.0 = progressive flash-freeze to glacial ice */
    bool fastMartialStrike;   /* Normalize ATLA phases to desc.duration, skip 0.48s ground puddle delay */
    bool allowCompactBounds;  /* Allow compact 1.8m x 2.4m bounds and 0.45s min duration for melee strikes */
} LiquidSWEDesc;

typedef struct {
    Vector3 position;
    Vector3 normal;
} LiquidSWESample;

void LiquidSWE_Init(void);
void LiquidSWE_Unload(void);
LiquidSWEDesc LiquidSWE_DefaultDesc(void);

int LiquidSWE_Spawn(const LiquidSWEDesc *desc);
void LiquidSWE_Kill(int handle);
void LiquidSWE_Stop(int handle);
void LiquidSWE_SetOrigin(int handle, Vector3 origin, Vector3 forward);
bool LiquidSWE_IsAnyActive(void);

void LiquidSWE_ClearEmitters(int handle);
void LiquidSWE_AddEmitter(int handle, const LiquidSWEEmitter *emitter);
void LiquidSWE_SetManifold(int handle, int slot, const LiquidSWEManifold *manifold);

void LiquidSWE_Update(float dt);
void LiquidSWE_Draw3D(Camera3D cam);
void LiquidSWE_DrawShadowPass(void);

bool LiquidSWE_GetStats(int handle, float *outMaxH, float *outTotalVolume, float *outMaxFoam);
bool LiquidSWE_GetCell(int handle, int ix, int iz, float *outH, float *outB, float *outFoam, float *outWetness);
float LiquidSWE_GetCryoProgress(int handle);

/* Deterministic 3D differential-geometry manifold evaluators (safe for headless tests) */
Vector3 LiquidSWE_EvalWhipSpine3D(float s, float tau, Vector2 origXZ, Vector2 targetXZ,
                                  float archH, float latCurve, float swaySign, float phase);
Vector3 LiquidSWE_EvalHydroTrajectory3D(float s, Vector2 origXZ, Vector2 targetXZ,
                                        float archH, float latCurve, float swaySign, float phase);
Vector3 LiquidSWE_EvalTwinHorizontalWhip3D(float uLong, float tau, Vector2 origXZ, Vector2 targetXZ,
                                           float planeH, float orbR, float swaySign, float phase);
LiquidSWESample LiquidSWE_EvalManifoldPoint(const LiquidSWEManifold *manifold,
                                            float uLong, float uCross, float localH);

#ifdef __cplusplus
}
#endif

#endif /* CORE_LIQUID_SWE_H */
