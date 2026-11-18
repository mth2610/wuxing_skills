// open_world/ow_weather.h
// Generic, data-driven Open-World Dynamic Weather & Natural Phenomena Engine.
// Consumes OWWeatherProfileDef tables from OWRealmDef — zero hardcoded realm logic.
#ifndef OW_WEATHER_H
#define OW_WEATHER_H

#include "open_world/ow_types.h"
#include "sandbox/sandbox_core.h"

void OWWeather_Init(const OWRealmDef *realm);
void OWWeather_Update(PlayerEntity *player, float dt);
void OWWeather_Draw3D(const PlayerEntity *player, Camera3D cam);
void OWWeather_DrawHUD(void);
void OWWeather_Clear(void);

// Immediately switch to a weather profile index (0..profileCount-1).
// If customDuration <= 0, uses the profile's defaultDurationSec.
void OWWeather_SetWeather(int profileIndex, float customDuration);
int  OWWeather_GetCurrentIndex(void);
int  OWWeather_GetProfileCount(void);
const OWWeatherProfileDef *OWWeather_GetCurrentProfile(void);
const OWWeatherProfileDef *OWWeather_GetProfile(int index);
float OWWeather_GetRemainingTime(void);

// Optional gameplay modifiers (1.0f = neutral)
float OWWeather_GetMoveSpeedMult(void);
float OWWeather_GetXpMult(void);

#endif // OW_WEATHER_H
