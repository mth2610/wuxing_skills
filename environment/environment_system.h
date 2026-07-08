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

Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);

// --- Time-of-Day dynamic lighting cycle ---
// Opt-in system layered on top of the static Environment_Set*() calls above.
// A map that never calls the functions below keeps its old, static, one-shot
// lighting behavior with ZERO changes: Environment_Update() only touches the
// blended state when a speed != 0 AND at least one preset has been set.
#define MAX_TIME_OF_DAY_PRESETS 8

typedef struct {
    Color        ambientColor;
    Color        sunColor;
    Vector3      sunDirection;
    Color        shadowColor;
    EnvFogConfig fog;
} EnvLightingPreset;

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
