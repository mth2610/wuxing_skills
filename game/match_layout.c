// game/match_layout.c
#include "game/match_layout.h"
#include "maps/map_manager.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float ClampFloat(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool IsPointPlayable(float x, float z, float *outY) {
    float gy = MapManager_GetGroundHeightAt(x, z);
    if (gy < -20.0f) return false;
    float surfY = 0.0f, depth = 0.0f;
    if (MapManager_GetWaterInfoAt(x, z, &surfY, &depth) && depth > 0.55f) {
        return false;
    }
    if (outY) *outY = gy;
    return true;
}

static Vector3 FindSafeGroundPoint(Vector3 candidate, Vector3 fallbackCenter) {
    float gy = 0.0f;
    if (IsPointPlayable(candidate.x, candidate.z, &gy)) {
        return (Vector3){ candidate.x, gy, candidate.z };
    }
    static const float kRadii[3] = { 1.8f, 3.6f, 5.5f };
    for (int r = 0; r < 3; r++) {
        for (int a = 0; a < 8; a++) {
            float ang = ((float)a / 8.0f) * 2.0f * PI;
            float px = candidate.x + cosf(ang) * kRadii[r];
            float pz = candidate.z + sinf(ang) * kRadii[r];
            if (IsPointPlayable(px, pz, &gy)) {
                return (Vector3){ px, gy, pz };
            }
        }
    }
    // Pull 35% toward fallbackCenter as last resort
    float mx = candidate.x * 0.65f + fallbackCenter.x * 0.35f;
    float mz = candidate.z * 0.65f + fallbackCenter.z * 0.35f;
    return (Vector3){ mx, MapManager_GetGroundHeightAt(mx, mz), mz };
}

void MatchLayout_Compute(Vector3 arenaCenter, float arenaRadius,
                         bool isEscortMode, int teamSize, MatchLayout *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->arenaCenter = arenaCenter;
    out->arenaRadius = arenaRadius;

    int clampedTeam = (teamSize < 1) ? 1 : (teamSize > MATCH_MAX_TEAM_SLOTS ? MATCH_MAX_TEAM_SLOTS : teamSize);
    float baseDist = isEscortMode
        ? ClampFloat(arenaRadius * 0.36f, 12.0f, 26.0f)
        : ClampFloat(arenaRadius * 0.22f, 8.0f, 14.0f);

    // Axis: Team 0 on West (-X), Team 1 on East (+X)
    for (int t = 0; t < 2; t++) {
        float dir = (t == 0) ? -1.0f : 1.0f;
        Vector3 rawBase = { arenaCenter.x + dir * baseDist, 0.0f, arenaCenter.z };
        out->teamBase[t] = FindSafeGroundPoint(rawBase, arenaCenter);

        // Hero formation slots: Slot 0 near center/Boss, flanks fanned +/-Z & slightly forward
        for (int s = 0; s < MATCH_MAX_TEAM_SLOTS; s++) {
            float lateral = 0.0f;
            float forward = 0.0f;
            if (clampedTeam == 1) {
                lateral = 0.0f;
                forward = 1.0f;
            } else {
                // Arc formation: Slot 0 slightly back/center (Guardian), others flank forward
                int sideSign = ((s & 1) == 0) ? -1 : 1;
                int ring = (s + 1) / 2;
                lateral = (s == 0) ? 0.0f : (float)sideSign * (1.8f + 1.2f * (float)(ring - 1));
                forward = (s == 0) ? -0.6f : 1.6f;
            }
            Vector3 rawHero = {
                out->teamBase[t].x - dir * forward,
                0.0f,
                out->teamBase[t].z + lateral
            };
            out->heroSpawns[t][s] = FindSafeGroundPoint(rawHero, out->teamBase[t]);
        }

        // Boss spawn: 3.8m behind/flanked from teamBase so heroes start ahead of Boss
        float laneSign = (t == 0) ? -1.0f : 1.0f;
        Vector3 rawBoss = {
            out->teamBase[t].x + dir * 3.8f,
            0.0f,
            out->teamBase[t].z + laneSign * 4.5f
        };
        out->bossSpawns[t] = FindSafeGroundPoint(rawBoss, arenaCenter);

        // Curved 5-waypoint dual-lane BossRoute converging at arenaCenter
        BossRoute *rt = &out->bossRoutes[t];
        rt->marchSpeedMps = 1.25f;
        rt->waypointCount = 5;
        float arcAmp = ClampFloat(baseDist * 0.42f, 5.5f, 11.0f);
        for (int w = 0; w < 5; w++) {
            float u = (float)(w + 1) / 5.0f; // 0.2 .. 1.0
            float x = out->bossSpawns[t].x * (1.0f - u) + arenaCenter.x * u;
            // Smooth sine arch along the team's flank, tapering to 0 at u=1.0
            float arch = sinf(u * PI) * arcAmp * laneSign * (1.0f - 0.35f * u);
            float z = arenaCenter.z + arch;
            rt->waypoints[w] = (w == 4)
                ? (Vector3){ arenaCenter.x, MapManager_GetGroundHeightAt(arenaCenter.x, arenaCenter.z), arenaCenter.z }
                : FindSafeGroundPoint((Vector3){ x, 0.0f, z }, arenaCenter);
        }
    }
}
