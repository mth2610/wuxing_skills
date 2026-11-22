// Rain mode: thin compatibility wrappers delegating to environment/weather_system.h.
#include "environment/weather_system.h"

void MapProp_SetRain(float intensity) { Weather_SetRain(intensity); }
float MapProp_GetRainTarget(void) { return Weather_GetRainTarget(); }
float MapProp_GetRain(void) { return Weather_GetRain(); }
float MapProp_GetRunoff(void) { return Weather_GetRunoff(); }
float MapProp_GetWetness(void) { return Weather_GetWetness(); }
void MapProp_UpdateRain(float dt) { (void)dt; /* Updated via Environment_Update -> Weather_Update */ }
void MapProp_DrawRain(Vector3 focus, float time) { Weather_DrawRain(focus, time); }
