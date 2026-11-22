// game/match_layout.h
// Symmetric, terrain-validated team spawn placement & dual-lane Boss route
// generator for MOBA Escort (1v1/2v2) and Team Battle modes.
#ifndef GAME_MATCH_LAYOUT_H
#define GAME_MATCH_LAYOUT_H

#include "boss/boss_system.h"
#include "raylib.h"
#include <stdbool.h>

#define MATCH_MAX_TEAM_SLOTS 4

typedef struct {
    Vector3   arenaCenter;
    float     arenaRadius;
    Vector3   teamBase[2];
    Vector3   heroSpawns[2][MATCH_MAX_TEAM_SLOTS];
    Vector3   bossSpawns[2];
    BossRoute bossRoutes[2];
} MatchLayout;

// Computes symmetric team bases, role-arranged hero spawns, and curved
// dual-lane Boss routes converging on arenaCenter. Every point is snapped to
// ground height and nudged out of cliffs or deep water.
void MatchLayout_Compute(Vector3 arenaCenter, float arenaRadius,
                         bool isEscortMode, int teamSize, MatchLayout *out);

#endif // GAME_MATCH_LAYOUT_H
