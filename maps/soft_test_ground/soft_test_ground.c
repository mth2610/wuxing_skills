#include "soft_test_ground.h"
#include "raylib.h"
#include "environment/environment_system.h"

// CORE_ISSUES.md Item 3 (Soft Particles) — a deliberately empty test arena:
// one flat opaque floor at EXACTLY Y=0.0f, nothing else (no props, no
// bamboo, no walls). Existing maps couldn't give a clean answer: bamboo_valley
// draws no real floor at all, and default_arena's floor sits at Y=-0.05f and
// is easy to confuse with other test evidence. This map exists purely so the
// soft-particle test sphere in skills/taiji/core_test has an unambiguous,
// isolated real ground plane to bury into, with zero other occluders nearby.
//
// Item 3 update: the floor used to be drawn via raw rlBegin(RL_TRIANGLES)/
// rlVertex3f/rlColor4ub immediate mode with no shader bound — the leading
// (now confirmed) hypothesis for why ground occlusion never registered for
// soft particles while ordinary DrawModelEx-drawn props (bamboo, player)
// worked fine. Switched to a real GenMeshPlane + DrawModel mesh draw so the
// floor goes through the normal mesh/shader/depth path like everything else.

static Model floorModel;
static bool floorModelLoaded = false;

void InitSoftTestGroundMap(void) {
    Environment_SetAmbientColor((Color){ 20, 24, 20, 255 });
    Environment_SetSunColor((Color){ 120, 130, 110, 255 });
    Environment_SetSunDirection((Vector3){ 0.4f, -1.0f, 0.6f });
    Environment_SetShadowColor((Color){ 4, 6, 4, 190 });

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){ 20, 24, 20, 255 };
    fog.start = 1200.0f;
    fog.end = 3000.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);

    // Same footprint as the old immediate-mode fan: a disc of radius 1805
    // centered on the arena center. GenMeshPlane gives a flat XZ quad grid;
    // 3610 x 3610 covers the same extent as the old radius-1805 circle
    // (a square superset, fine for a test floor with nothing near its edges).
    Mesh mesh = GenMeshPlane(3610.0f, 3610.0f, 4, 4);
    floorModel = LoadModelFromMesh(mesh);
    floorModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = GetColor(0x22331FFF);
    floorModelLoaded = true;
}

void DrawSoftTestGroundMap(void) {
    Vector3 center = { 600.0f, 0.0f, 440.0f };
    DrawModel(floorModel, center, 1.0f, GetColor(0x22331FFF));
}

void UnloadSoftTestGroundMap(void) {
    if (floorModelLoaded) {
        UnloadModel(floorModel);
        floorModelLoaded = false;
    }
}
