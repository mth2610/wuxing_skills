// Snowfall weather system: thin compatibility wrappers delegating to environment/weather_system.h.
#include "environment/weather_system.h"

void MapProp_SetSnow(float intensity) { Weather_SetSnow(intensity); }
float MapProp_GetSnow(void) { return Weather_GetSnow(); }
void MapProp_UpdateSnow(float dt) { (void)dt; /* Updated via Environment_Update -> Weather_Update */ }
void MapProp_DrawSnow(Vector3 focus, Camera3D cam, float time) { Weather_DrawSnow(focus, cam, time); }
