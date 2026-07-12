// formations/han_bang_thuy_tuyet_def.c — Hàn Băng Thủy Tuyệt Trận: a frost
// field that slows every enemy inside (speedMult modifier, refreshed each
// tick). Trên Sông (NAT_RIVER) → cộng hưởng: deeper slow. DATA half.
#include "formations/formation_system.h"
#include "core/skill_manager.h" // ELEMENT_COLOR_WATER
#include "raylib.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const float BANG_RADIUS    = 3.5f;
static const float BANG_DURATION  = 10.0f;
static const float BANG_MANA_COST = 30.0f;
static const float BANG_SLOW_BASE = 0.6f;  // speedMult 0.6; resonant → 0.4

static float s_refreshAccum = 0.0f;

static void HanBangTick(Vector3 center, float dt, float power, AgentTeam ownerTeam) {
    // Refresh the slow every 0.5s with a short duration so leaving the
    // circle recovers speed quickly (modifier slots are find-first-empty —
    // short refreshes avoid stacking four copies).
    s_refreshAccum += dt;
    if (s_refreshAccum < 0.5f) return;
    s_refreshAccum -= 0.5f;

    float slow = (power > 1.0f) ? BANG_SLOW_BASE - 0.2f : BANG_SLOW_BASE;
    AgentTeam victimTeam = (ownerTeam == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
    Entity_ApplyAoEBuff(center, BANG_RADIUS, slow, 0.6f, victimTeam);
}

static void HanBangDraw(Vector3 center, float t, float power) {
    Color c = ELEMENT_COLOR_WATER;
    Vector3 p = { center.x, 0.03f, center.z };
    DrawCircle3D(p, BANG_RADIUS, (Vector3){ 1, 0, 0 }, 90.0f, c);
    // Six slow-orbiting frost petals (perpendicular jitter per art law).
    int petals = (power > 1.0f) ? 9 : 6;
    for (int i = 0; i < petals; i++) {
        float a = t * 0.6f + (2.0f * PI * i) / petals;
        Vector3 q = { center.x + cosf(a) * BANG_RADIUS * 0.7f,
                      0.06f + 0.03f * sinf(t * 2.0f + i),
                      center.z + sinf(a) * BANG_RADIUS * 0.7f };
        DrawCircle3D(q, 0.18f, (Vector3){ 1, 0, 0 }, 90.0f + a * 57.0f, c);
    }
}

const FormationDef FORMATION_HAN_BANG_THUY_TUYET = {
    .name = "HAN_BANG_THUY_TUYET",
    .elem = ELEM_WATER,
    .radius = BANG_RADIUS,
    .duration = BANG_DURATION,
    .manaCost = BANG_MANA_COST,
    .resonantZone = NAT_RIVER,
    .onTick = HanBangTick,
    .drawGround = HanBangDraw,
};
