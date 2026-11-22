#ifndef ENVIRONMENT_WEATHER_SYSTEM_H
#define ENVIRONMENT_WEATHER_SYSTEM_H

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

void Weather_Init(void);
void Weather_Update(float dt);

// Rain mode (intensity: 0 = dry, 1 = ~65 mm/h heavy, 2 = storm)
void Weather_SetRain(float intensity);
float Weather_GetRainTarget(void);
float Weather_GetRain(void);
float Weather_GetRunoff(void);
float Weather_GetWetness(void);
void Weather_DrawRain(Vector3 focus, float time);

// Snow mode (intensity: 0 = clear, 1 = gentle snowfall, 2 = blizzard)
void Weather_SetSnow(float intensity);
float Weather_GetSnow(void);
void Weather_DrawSnow(Vector3 focus, Camera3D cam, float time);

#ifdef __cplusplus
}
#endif

#endif // ENVIRONMENT_WEATHER_SYSTEM_H
