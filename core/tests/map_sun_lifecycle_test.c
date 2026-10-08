/* Exercise actual manager activation with a synthetic registry and environment.
 * GPU lighting uploads and map rendering remain integration-test concerns. */
#include <stdbool.h>
#include <stdio.h>
#include "core/map_manager.h"

#define MAPS_GENERATED_H
#define ENVIRONMENT_SYSTEM_H
static float intensity = 1.0f;
static int failures;
static int defaultInitCount;
#define CHECK(c, label) do { if (!(c)) { puts("FAIL: " label); failures++; } } while (0)

void Environment_SetSunIntensity(float value) { intensity = value; }
static void DrawCircle3D(Vector3 center, float radius, Vector3 axis, float angle, Color color) {
    (void)center; (void)radius; (void)axis; (void)angle; (void)color;
}
static void InitDefault(void) {
    CHECK(intensity == 1.0f, "default map sees neutral solar intensity before Init");
    defaultInitCount++;
}
static void InitHDR(void) {
    CHECK(intensity == 1.0f, "HDR map sees neutral solar intensity before opt-in");
    Environment_SetSunIntensity(3.0f);
}
static void RegisterGeneratedMaps(void) {
    MapManager_Register("DEFAULT_ARENA", InitDefault, NULL, NULL, NULL);
    MapManager_Register("HDR", InitHDR, NULL, NULL, NULL);
    MapManager_Register("NO_INIT", NULL, NULL, NULL, NULL);
}
#include "core/map_manager.c"

int main(void) {
    intensity = 7.0f;
    MapManager_Init();
    CHECK(defaultInitCount == 1 && intensity == 1.0f, "initial activation restores legacy sun");
    MapManager_SetActiveIndex(1);
    CHECK(intensity == 3.0f, "HDR map may opt in after activation reset");
    MapManager_SetActiveIndex(-1);
    CHECK(intensity == 3.0f, "invalid activation leaves active lighting unchanged");
    MapManager_SetActiveIndex(0);
    CHECK(defaultInitCount == 2 && intensity == 1.0f, "HDR sun cannot leak to loaded default map");
    MapManager_SetActiveIndex(1);
    CHECK(intensity == 3.0f, "HDR reactivation restores authored intensity");
    MapManager_SetActiveIndex(2);
    CHECK(intensity == 1.0f, "maps without Init also receive legacy intensity");
    MapManager_SetActiveIndex(1);
    MapManager_Unload();
    CHECK(intensity == 1.0f, "unload leaves neutral solar state");
    if (failures) return 1;
    puts("PASS: map activation isolates opt-in HDR solar intensity");
    return 0;
}
