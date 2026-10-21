#ifndef ENVIRONMENT_SYSTEM_H
#define ENVIRONMENT_SYSTEM_H

#include "raylib.h"

typedef enum
{
    ENV_SHAPE_SPHERE,
    ENV_SHAPE_CYLINDER,
    ENV_SHAPE_BOX
} EnvShadowShapeType;

typedef struct
{
    Color color;
    float start;
    float end;
    float density;
    bool enabled;
} EnvFogConfig;

// Khởi tạo và cập nhật môi trường
void Environment_Init(void);
void Environment_Update(float dt);

// Vẽ bóng giả thông minh (Smart Fake Shadow) và gom nhóm batch tránh flush pipeline
void Environment_BeginShadowBatch(void);
void Environment_EndShadowBatch(void);
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);

// Các hàm Getter / Setter cấu hình ánh sáng mặt trời
Vector3 Environment_GetSunDirection(void);
void Environment_SetSunDirection(Vector3 dir);

Color Environment_GetSunColor(void);
void Environment_SetSunColor(Color col);
float Environment_GetSunIntensity(void);
void Environment_SetSunIntensity(float intensity);

Color Environment_GetAmbientColor(void);
void Environment_SetAmbientColor(Color col);

Color Environment_GetSkyAmbient(void);
Color Environment_GetGroundAmbient(void);

Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);

// =========================================================================
// Advanced Volumetric Atmosphere & Fog (Ghost of Tsushima & Modern Optics)
// =========================================================================

typedef enum
{
    FOG_SHAPE_SPHERE,
    FOG_SHAPE_BOX,
    FOG_SHAPE_CYLINDER
} FogVolumeShape;

#define MAX_LOCAL_FOG_VOLUMES 32

typedef struct
{
    int id;
    FogVolumeShape shape;
    Vector3 position;
    Vector3 extents;
    Color color;
    float density;
    float edgeSoftness;
    float emissive;
    Vector3 driftVelocity;
    float lifetime;
    float maxLifetime;
    bool active;
} LocalFogVolume;

typedef struct
{
    Vector3 rayleighLMS;
    float mieScattering;
    float mieAnisotropy;
    float multipleScatteringAmp;
} OpticalScatteringCoeffs;

typedef struct
{
    float baseDensity;
    float heightFalloff;
    float baseAltitude;
    bool enableSigmoidLayer;
    float layerAltitude;
    float layerThickness;
    float layerDensity;
} AtmosphericDensityProfile;

typedef struct
{
    Color color;
    float start;
    float end;
    bool enabled;
    OpticalScatteringCoeffs optics;
    AtmosphericDensityProfile density;
} AtmosphereProfile;

AtmosphereProfile Environment_GetAtmosphereProfile(void);
void Environment_SetAtmosphereProfile(const AtmosphereProfile *profile);

// Local Fog Volumes (props, map points, skill VFX)
int FogVolume_Create(const LocalFogVolume *volume);
void FogVolume_Update(int id, const LocalFogVolume *volume);
void FogVolume_Destroy(int id);
void FogVolume_ClearAll(void);
int FogVolume_SpawnTransient(Vector3 pos, float radius, Color color, float density, float duration);
int FogVolume_GetActiveCount(void);
const LocalFogVolume *FogVolume_GetByIndex(int index);
const LocalFogVolume *FogVolume_GetById(int id);

#define MAX_TIME_OF_DAY_PRESETS 8

typedef struct
{
    Color ambientColor;
    Color sunColor;
    Vector3 sunDirection;
    Color shadowColor;
    EnvFogConfig fog;
    AtmosphereProfile atmosphere;
} EnvLightingPreset;

typedef struct
{
    Vector3 sunDirection;
    Color sunColor;
    Color skyAmbient;
    Color groundBounce;
    Color shadowColor;
    EnvFogConfig fog;
    AtmosphereProfile atmosphere;
    unsigned int version;
    float sunIntensity;
} EnvFrameLighting;

EnvFrameLighting Environment_GetFrameLighting(void);

typedef struct
{
    bool enabled;
    float strength;
    float worldSize;
    float planeHeight;
    float coverage;
    float softness;
    float windSpeedScale;
} EnvCloudShadowConfig;

typedef struct
{
    Texture2D noiseTexture;
    Vector4 uvTransform;
    Vector4 shape;
    Vector2 projection;
    unsigned int version;
} EnvCloudShadowFrame;

EnvCloudShadowConfig Environment_GetCloudShadowConfig(void);
void Environment_SetCloudShadowConfig(const EnvCloudShadowConfig *config);
EnvCloudShadowFrame Environment_GetCloudShadowFrame(void);
void Environment_BindCloudShadowShader(Shader shader);

void Environment_ApplyProfile(const EnvLightingPreset *profile);

void Environment_SetTimeOfDayPresets(const EnvLightingPreset *presets, const float *timePoints, int count);
void Environment_SetTimeOfDaySpeed(float cyclesPerSecond);
void Environment_SetTimeOfDay(float t);
float Environment_GetTimeOfDay(void);

#endif // ENVIRONMENT_SYSTEM_H