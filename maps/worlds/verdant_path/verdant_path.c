#include "verdant_path.h"
#include "raylib.h"
#include "environment/environment_system.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_props.h"

// Rectangle sized so a corner-to-corner diagonal walk takes ~30-45s at the
// game screen's 3.5 m/s walk speed (game/game_screen.c): 100m x 75m is a
// 4:3 rectangle (a 3-4-5 triangle scaled x25), giving an exact 125m
// diagonal -> 125 / 3.5 = ~35.7s.
#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

// Stone path runs along the long (X) axis, centered on the map. Kept well
// short of the flat plateau's own edge (~45m from center before the cliff
// falloff starts, see generate_island_heightmap.py's plateau_edge=0.90) so
// the path never reaches the point where the ground drops away underneath
// it — a too-long path used to visibly "float" past the cliff edge over
// the cloud sea.
#define PATH_LENGTH 50.0f
#define PATH_WIDTH 4.0f

// Floating-island shape (MapProp_CreateGroundHeightmap): plateau at Y=0,
// edge sinks CLIFF_DEPTH meters down. CLOUD_SEA_Y must stay more negative
// than -CLIFF_DEPTH so the cliff mesh never pokes through the cloud plane.
#define CLIFF_DEPTH 8.0f
#define CLOUD_SEA_Y -12.0f

// Mountain-ring rocks don't sample the heightmap — they sink a fixed amount
// assuming flat Y=0 ground (MapProp_DrawRocks' convention). Kept comfortably
// inside the flat plateau boundary (~45m/~33.75m from center — see
// generate_island_heightmap.py's plateau_edge=0.90) so the ring never lands
// on the sloped cliff band, which would leave a visible gap/seam between
// rock bottom and the (already-lower) actual ground surface there.
#define MOUNTAIN_RING_WIDTH 80.0f
#define MOUNTAIN_RING_DEPTH 56.0f

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
static MapCloudSea s_cloudSea;
static bool s_ready = false;

// Floating-island motif every map shares (kehoach/world direction): a ring
// of giant mountain rocks bordering the playable ground, with a scrolling
// sea of clouds far below. Reuses s_rocks' own model/texture at a much
// bigger scale — no separate mountain texture set needed.
#define MOUNTAIN_ROCK_COUNT 36
static MapRockPlacement s_mountainRocks[MOUNTAIN_ROCK_COUNT];

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

    // Nền dạng đảo nổi: cao nguyên phẳng ở giữa (từ heightmap trắng), sụp
    // xuống thành vách đá ở viền (đen) — CLIFF_DEPTH phải nhỏ hơn độ sâu
    // yOffset của MapProp_DrawCloudSea bên dưới để vách không đâm xuyên mây.
    s_ground = MapProp_CreateGroundHeightmap("assets/heightmaps/verdant_path_island.png",
                                    MAP_WIDTH, MAP_DEPTH, CLIFF_DEPTH, 12.0f,
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

    // Mountain ring: same rock model/texture as scattered rocks, bordering
    // the map. Low profile on purpose — just a bit taller than ground level
    // (heightScale small), NOT towering peaks; the actual cliff drop comes
    // from the heightmap ground itself (CLIFF_DEPTH), these just mark the
    // edge. Fixed seed so the layout is reproducible across runs.
    // Generated in its own smaller [0,MOUNTAIN_RING_WIDTH]x[0,MOUNTAIN_RING_DEPTH]
    // space, then re-centered onto the map's actual coordinate system below —
    // keeps the ring on the flat plateau instead of the true (sloped) edge.
    MapProp_GenerateMountainRing(s_mountainRocks, MOUNTAIN_ROCK_COUNT,
                                 MOUNTAIN_RING_WIDTH, MOUNTAIN_RING_DEPTH,
                                 3.0f, 6.0f,   // radiusScale range
                                 1.0f, 2.5f,   // heightScale range
                                 1337);
    {
        float offsetX = (MAP_WIDTH - MOUNTAIN_RING_WIDTH) * 0.5f;
        float offsetZ = (MAP_DEPTH - MOUNTAIN_RING_DEPTH) * 0.5f;
        for (int i = 0; i < MOUNTAIN_ROCK_COUNT; i++) {
            s_mountainRocks[i].position.x += offsetX;
            s_mountainRocks[i].position.z += offsetZ;
        }
    }

    // Sea of clouds, far below the ground — bigger than the map so it reads
    // as an endless void floor past the mountain ring.
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);

    s_ready = true;
}

void DrawVerdantPathMap(void)
{
    if (!s_ready)
        return;

    PropLit_UpdateLighting(); // per-frame contract for s_path/s_rocks (both prop_lit)

    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawStrip(&s_path, kMapCenter, 0.01f);
    MapProp_DrawRocks(&s_rocks, s_mountainRocks, MOUNTAIN_ROCK_COUNT);
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT);
}
