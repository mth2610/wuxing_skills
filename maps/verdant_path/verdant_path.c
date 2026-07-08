#include "verdant_path.h"
#include "raylib.h"
#include "environment/environment_system.h"
#include "core/prop_lit.h"
#include "maps/common/map_props.h"

// Rectangle sized so a corner-to-corner diagonal walk takes ~30-45s at the
// game screen's 3.5 m/s walk speed (game/game_screen.c): 100m x 75m is a
// 4:3 rectangle (a 3-4-5 triangle scaled x25), giving an exact 125m
// diagonal -> 125 / 3.5 = ~35.7s.
#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

// Stone path runs along the long (X) axis, centered on Z.
#define PATH_LENGTH (MAP_WIDTH - 10.0f)
#define PATH_WIDTH 4.0f

// Scattered across the field, clear of the path band (Z in
// [MAP_DEPTH/2 - PATH_WIDTH, MAP_DEPTH/2 + PATH_WIDTH]).
#define ROCK_COUNT 6
static const MapRockPlacement kRocks[ROCK_COUNT] = {
    {{15.0f, 0.0f, 10.0f}, 0.6f, 0.5f, 20.0f},
    {{30.0f, 0.0f, 60.0f}, 0.9f, 0.7f, 100.0f},
    {{70.0f, 0.0f, 15.0f}, 0.5f, 0.45f, 200.0f},
    {{85.0f, 0.0f, 55.0f}, 1.1f, 0.8f, 60.0f},
    {{45.0f, 0.0f, 65.0f}, 0.7f, 0.55f, 320.0f},
    {{20.0f, 0.0f, 45.0f}, 0.8f, 0.6f, 150.0f},
};

static MapGroundSurface s_ground;
static MapStripSurface s_path;
static MapRockSet s_rocks;
static bool s_ready = false;

void InitVerdantPathMap(void)
{
    // Cool, clear moonlight — bright enough to read the ground/rock
    // textures clearly, while staying within the project's always-night
    // identity (see nguhanhtyvo_kehoach.md §I).
    Environment_SetAmbientColor((Color){60, 65, 85, 255});
    Environment_SetSunColor((Color){200, 205, 220, 255});
    Environment_SetSunDirection((Vector3){0.5f, -0.8f, -0.3f}); // project-standard direction
    Environment_SetShadowColor((Color){10, 10, 15, 150});

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){40, 45, 60, 255};
    fog.start = 60.0f;
    fog.end = 140.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);

    // [ĐÃ SỬA LỖI 1]: Gán s_ground và truyền đúng kích thước toàn map (MAP_WIDTH, MAP_DEPTH)
    s_ground = MapProp_CreateGround(MAP_WIDTH, MAP_DEPTH, 12.0f,
                                    "assets/textures/grass_ground_diffuse.png", // Tạm dùng làm Splatmap
                                    "assets/textures/grass_ground_diffuse.png", // Texture Cỏ
                                    "assets/textures/dirt_diffuse.png");        // Tạm dùng làm Texture Đường đi

    // [ĐÃ SỬA LỖI 2]: Truyền đúng PATH_LENGTH và PATH_WIDTH cho con đường
    s_path = MapProp_CreateStrip(PATH_LENGTH, PATH_WIDTH, 2.0f,
                                 "assets/textures/stone_path_diffuse.png",
                                 "assets/textures/stone_path_normal.png",
                                 "assets/textures/stone_path_roughness.png");

    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
                                  "assets/textures/rock_normal.png",
                                  "assets/textures/rock_roughness.png");

    s_ready = true;
}

void DrawVerdantPathMap(void)
{
    if (!s_ready)
        return;

    PropLit_UpdateLighting(); // per-frame contract for s_path/s_rocks (both prop_lit)

    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawStrip(&s_path, kMapCenter, 0.01f);
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT);
}
