#ifndef ENVIRONMENT_SYSTEM_H
#define ENVIRONMENT_SYSTEM_H

#include "raylib.h"

typedef enum {
    ENV_SHAPE_SPHERE,
    ENV_SHAPE_CYLINDER,
    ENV_SHAPE_BOX
} EnvShadowShapeType;

typedef struct {
    Color color;
    float start;
    float end;
    float density;
    bool enabled;
} EnvFogConfig;

// Khởi tạo và cập nhật môi trường
void Environment_Init(void);
void Environment_Update(float dt);

// Vẽ bóng giả thông minh (Smart Fake Shadow)
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);

// Các hàm Getter / Setter cấu hình ánh sáng mặt trời
Vector3 Environment_GetSunDirection(void);
void Environment_SetSunDirection(Vector3 dir);

Color Environment_GetSunColor(void);
void Environment_SetSunColor(Color col);

Color Environment_GetAmbientColor(void);
void Environment_SetAmbientColor(Color col);

// Real Shading P1c — hemispheric ambient split for surface_lit (upper-sky
// tint vs. lower-ground bounce), derived from the flat ambient above.
Color Environment_GetSkyAmbient(void);
Color Environment_GetGroundAmbient(void);

Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);

// =========================================================================
// Advanced Volumetric Atmosphere & Fog (Ghost of Tsushima & Modern Optics)
// =========================================================================

typedef enum {
    FOG_SHAPE_SPHERE,
    FOG_SHAPE_BOX,
    FOG_SHAPE_CYLINDER
} FogVolumeShape;

#define MAX_LOCAL_FOG_VOLUMES 32

typedef struct {
    int            id;              // Non-zero handle
    FogVolumeShape shape;
    Vector3        position;
    Vector3        extents;         // Sphere: x=radius; Box: half-extents (x,y,z); Cylinder: x=radius, y=half-height
    Color          color;           // Albedo / tint
    float          density;         // Absorption + scattering density
    float          edgeSoftness;    // 0.0=hard edge, 1.0=smooth cubic fade
    float          emissive;        // Glow/luminance (useful for skill VFX)
    Vector3        driftVelocity;   // Wind drift movement
    float          lifetime;        // Remaining life (seconds); <=0 means permanent
    float          maxLifetime;     // Total duration (for fade-out curve)
    bool           active;
} LocalFogVolume;

// Optical scattering coefficients in LMS colour space (Patry/Schuler SIGGRAPH 2021)
typedef struct {
    Vector3 rayleighLMS;            // Rayleigh scattering in LMS space [km^-1] (default: 0.0076224, 0.012935, 0.024845)
    float   mieScattering;          // Mie scattering coefficient
    float   mieAnisotropy;          // Phase function forward-scattering g in [0.70..0.85]
    float   multipleScatteringAmp;  // Multiple scattering boost (~2.16 for albedo 0.9)
} OpticalScatteringCoeffs;

// Analytical height density distribution: Exponential falloff + Sigmoid thermal inversion layer
typedef struct {
    float   baseDensity;            // Fog density at reference altitude
    float   heightFalloff;          // Exponential decay rate k_e along Y axis (0 = uniform)
    float   baseAltitude;           // Reference ground altitude (Y)
    bool    enableSigmoidLayer;     // Enables valley fog / sea of clouds inversion layer
    float   layerAltitude;          // Center altitude of cloud/fog blanket
    float   layerThickness;         // Transition thickness k_s of sigmoid curve
    float   layerDensity;           // Peak density of the blanket layer
} AtmosphericDensityProfile;

// Complete Atmosphere & Fog Profile (superset of EnvFogConfig)
typedef struct {
    Color                     color;         // Primary ambient fog albedo
    float                     start;         // Near distance cutoff
    float                     end;           // Far distance cutoff
    bool                      enabled;       // Global master switch
    OpticalScatteringCoeffs   optics;        // Physical scattering
    AtmosphericDensityProfile density;       // Height and inversion profiles
} AtmosphereProfile;

AtmosphereProfile Environment_GetAtmosphereProfile(void);
void              Environment_SetAtmosphereProfile(const AtmosphereProfile *profile);

// Local Fog Volumes (props, map points, skill VFX)
int                     FogVolume_Create(const LocalFogVolume *volume);
void                    FogVolume_Update(int id, const LocalFogVolume *volume);
void                    FogVolume_Destroy(int id);
void                    FogVolume_ClearAll(void);
int                     FogVolume_SpawnTransient(Vector3 pos, float radius, Color color, float density, float duration);
int                     FogVolume_GetActiveCount(void);
const LocalFogVolume*   FogVolume_GetByIndex(int index);
const LocalFogVolume*   FogVolume_GetById(int id);

#define MAX_TIME_OF_DAY_PRESETS 8

typedef struct {
    Color             ambientColor;
    Color             sunColor;
    Vector3           sunDirection;
    Color             shadowColor;
    EnvFogConfig      fog;
    AtmosphereProfile atmosphere;
} EnvLightingPreset;

// --- Resolved Frame Lighting Snapshot (E10) ---
typedef struct {
    Vector3           sunDirection;  // Normalized travel direction (from light into scene)
    Color             sunColor;      // Direct linear sunlight
    Color             skyAmbient;    // Upper hemisphere sky fill
    Color             groundBounce;  // Lower hemisphere warm earth bounce
    Color             shadowColor;   // Directional shadow attenuation
    EnvFogConfig      fog;           // Distance and height fog parameters
    AtmosphereProfile atmosphere;    // Full physical atmosphere profile
    unsigned int      version;       // Increments when lighting changes
} EnvFrameLighting;

// Supplies one resolved snapshot; consumers do not independently infer light direction/tint.
EnvFrameLighting Environment_GetFrameLighting(void);

// Validates and activates a complete profile atomically, incrementing lighting version.
void Environment_ApplyProfile(const EnvLightingPreset *profile);

// --- Time-of-Day dynamic lighting cycle ---
// Opt-in system layered on top of the static Environment_Set*() calls above.
// A map that never calls the functions below keeps its old, static, one-shot
// lighting behavior with ZERO changes: Environment_Update() only touches the
// blended state when a speed != 0 AND at least one preset has been set.

// Defines the keyframes of a full lighting cycle (e.g. dawn/noon/dusk/night).
// - `timePoints` are normalized [0,1), MUST be sorted ascending, count <= MAX_TIME_OF_DAY_PRESETS.
// - The cycle wraps: the segment from timePoints[count-1] up to 1.0/0.0 and
//   back to timePoints[0] is interpolated too (NOT a hard cut back to preset[0]).
// - Calling this overwrites any previously set presets.
// - All presets passed together MUST agree on `fog.enabled` (a bool can't be
//   interpolated — Environment_Update() just takes it from one of the two
//   bracketing presets, so mixing true/false across presets gives you an
//   arbitrary flicker between them; keep it consistent across the whole set).
void  Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count);

// Cycle speed in cycles-per-second (e.g. 1.0f/1200.0f for a 20-minute
// real-time full day/night loop). Default 0 = paused/disabled. When 0, or
// when no presets have been set via Environment_SetTimeOfDayPresets(), the
// system is fully inert: Environment_Update() behaves identically to before
// this feature existed, and a map's static Environment_Set*() Init() calls
// remain in full effect, unaffected.
void  Environment_SetTimeOfDaySpeed(float cyclesPerSecond);

// Manually jump to a point in the cycle. `t` is normalized and wrapped into [0,1).
void  Environment_SetTimeOfDay(float t);
float Environment_GetTimeOfDay(void);

#endif // ENVIRONMENT_SYSTEM_H
