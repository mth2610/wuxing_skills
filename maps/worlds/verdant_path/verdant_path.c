#include "verdant_path.h"
#include "raylib.h"
#include "rlgl.h"
#include "environment/environment_system.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_props.h"
#include "maps/toolkit/ground_shadow.h"
#include "core/map_manager.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Virtual Trigger Zones (MAP_API.md §13) — the grass island's nature
// pockets, all on the flat plateau interior (y = 0 there; heightmap only
// falls off past ~34m from center (50, 37.5)). Modifier rules live in
// game/game_rules.c, never here.
static const MapZone ISLAND_ZONES[] = {
    { NAT_RIVER,       { 40.0f, 0.0f, 30.0f }, 4.0f },
    { NAT_FOREST,      { 61.0f, 0.0f, 31.0f }, 4.5f },
    { NAT_DESERT_ZONE, { 50.0f, 0.0f, 47.0f }, 4.0f },
};
#define ISLAND_ZONE_COUNT (int)(sizeof(ISLAND_ZONES) / sizeof(ISLAND_ZONES[0]))

// Radial-gradient ground disc, self-lit vertex colors (alpha 255 rule; lit
// materials read black in the night scene) — same technique as
// default_arena's zone cues, lifted slightly above the terrain mesh.
static void DrawIslandZoneDisc(Vector3 center, float radius, Color cCenter, Color cEdge)
{
    rlDisableBackfaceCulling();
    rlSetTexture(0);
    GroundShadow_Begin(); // Real Shading P6 — receive the real shadow map (no-op if disabled)
    rlBegin(RL_TRIANGLES);
    int segments = 48;
    float y = 0.05f;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, y, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, y, center.z + sinf(a2) * radius };
        rlColor4ub(cCenter.r, cCenter.g, cCenter.b, 255);
        rlVertex3f(center.x, y, center.z);
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cEdge.r, cEdge.g, cEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    GroundShadow_End();
    rlEnableBackfaceCulling();
}

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
static MapRockSet s_mountainRockSet; // separate, plain-textured (no prop_lit, no shadow) — see InitVerdantPathMap
static MapCloudSea s_cloudSea;
static bool s_ready = false;

// Floating-island motif every map shares (kehoach/world direction): a ring
// of mountain rocks bordering the playable ground, with a scrolling sea of
// clouds far below. Same rock_diffuse.png texture as s_rocks, but its own
// MapRockSet (s_mountainRockSet, plain material, no shadow) — see
// InitVerdantPathMap.
#define MOUNTAIN_ROCK_COUNT 36
static MapRockPlacement s_mountainRocks[MOUNTAIN_ROCK_COUNT];

void InitVerdantPathMap(void)
{
    // Cool, clear moonlight — bright enough to read the ground/rock
    // textures clearly, while staying within the project's always-night
    // identity (see nguhanhtyvo_kehoach.md §I).
    //
    // Dimmed from the original (60,65,85)/(200,205,220): bloom is a
    // whole-screen effect (core/post_fx.c's bloomTex is shared, not
    // per-object — see post_process.fs's `sceneCol.rgb += bloomTex`), and
    // prop_lit.fs's `albedo * (ambient + diff*sunColor)` pushed sun-facing
    // grass patches above the 0.5 bloom threshold at the old brightness.
    // That made grass itself bloom and bleed into any nearby skill VFX's
    // bloom halo, visibly tinting/washing out the VFX's own color compared
    // to a map with a dark, non-blooming floor (default_arena). Reduced so
    // lit grass stays under threshold; skill VFX bloom is unaffected since
    // it comes from the VFX's own emissive shader, not this ambient/sun.
    Environment_SetAmbientColor((Color){38, 42, 55, 255});
    Environment_SetSunColor((Color){125, 130, 145, 255});
    Environment_SetSunDirection((Vector3){0.5f, -0.7f, -0.3f}); // Real Shading P6 — moderate elevation (was y=-0.8 near-steep, then debug-era y=-0.5): visible raking shadow without over-stretching
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

    // Mountain ring: its OWN plain-textured MapRockSet (NULL normal/roughness
    // -> no prop_lit) — a border of dozens of rocks running the full PBR
    // lighting shader per-pixel over a large chunk of the screen was a real,
    // measured FPS cost, not just the scattered decorative rocks' concern.
    s_mountainRockSet = MapProp_CreateRocks("assets/textures/rock_diffuse.png", NULL, NULL);

    // Low profile on purpose — just a bit taller than ground level
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

    MapManager_SetZones(ISLAND_ZONES, ISLAND_ZONE_COUNT);

    s_ready = true;
}

float GetGroundHeightVerdantPathMap(float x, float z)
{
    return MapProp_SampleGroundHeight(&s_ground, kMapCenter, x, z);
}

bool SampleGroundSurfaceVerdantPathMap(float x, float z, Vector3 *outPosition, Vector3 *outNormal)
{
    return MapProp_SampleGroundSurface(&s_ground, kMapCenter, x, z, outPosition, outNormal);
}

void DrawVerdantPathMap(void)
{
    if (!s_ready)
        return;

    PropLit_UpdateLighting(); // per-frame contract for s_path/s_rocks (both prop_lit)

    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawStrip(&s_path, kMapCenter, 0.01f);
    MapProp_DrawRocks(&s_mountainRockSet, s_mountainRocks, MOUNTAIN_ROCK_COUNT, false); // no shadow, plain material
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT, true); // decorative — keep shadow + prop_lit

    // Nature-zone cues (No Tutorial — the ground itself says what it is):
    // deep-water blue, forest green, dry sand, all fading into the grass.
    Color grassEdge = (Color){ 26, 38, 24, 255 };
    DrawIslandZoneDisc(ISLAND_ZONES[0].center, ISLAND_ZONES[0].radius, (Color){ 30, 84, 118, 255 }, grassEdge);
    DrawIslandZoneDisc(ISLAND_ZONES[1].center, ISLAND_ZONES[1].radius, (Color){ 22, 70, 38, 255 },  grassEdge);
    DrawIslandZoneDisc(ISLAND_ZONES[2].center, ISLAND_ZONES[2].radius, (Color){ 96, 80, 42, 255 },  grassEdge);
}
