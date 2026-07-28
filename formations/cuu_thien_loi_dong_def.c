// formations/cuu_thien_loi_dong_def.c — Cửu Thiên Lôi Động Trận: periodic
// thunder pulses stun enemies caught in the circle. Đặt trên Sông (NAT_RIVER)
// → cộng hưởng (power 1.5: longer stun, faster pulses). DATA half — this
// file may include VFX headers (drawGround), onTick stays Entity_*-only.
#include "formations/formation_system.h"
#include "core/composition/visual_composer.h"
#include "core/skill_manager.h" // ELEMENT_COLOR_TAIJI accent
#include "raylib.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const float LOI_RADIUS      = 4.0f;
static const float LOI_DURATION    = 8.0f;
static const float LOI_MANA_COST   = 35.0f;
static const float LOI_PULSE_EVERY = 1.5f; // seconds between stun pulses
static const float LOI_STUN_BASE   = 0.6f; // × power on resonance

static float s_pulseAccum = 0.0f;

static void LoiDongTick(Vector3 center, float dt, float power, AgentTeam ownerTeam) {
    // Faster pulses + longer stun when resonant.
    s_pulseAccum += dt * power;
    if (s_pulseAccum < LOI_PULSE_EVERY) return;
    s_pulseAccum -= LOI_PULSE_EVERY;

    // Stun every non-owner-team agent inside the circle; a sky bolt marks
    // each victim (visual feedback belongs to the data half).
    AgentTeam victimTeam = (ownerTeam == TEAM_ALLY) ? TEAM_ENEMY : TEAM_ALLY;
    int ids[16];
    int n = Entity_GetNearbyTargetsTeam(center, LOI_RADIUS, victimTeam, ids, 16);
    for (int i = 0; i < n; i++) {
        const Agent *victim = Entity_GetAgent(ids[i]);
        if (!victim) continue;
        Entity_ApplyStun(ids[i], LOI_STUN_BASE * power);
        Vector3 ground = { victim->position.x, 0.0f, victim->position.z };
        Vector3 sky = { ground.x, 12.0f, ground.z };
        // F0 purge: the sky->ground proc beam is deleted and has no one-shot
        // successor (VFX_ComposeLightShaft is the nearest survivor, but it is
        // CONTINUOUS — fired once it draws a single frame and reads as a
        // flicker). The strike keeps its impact; the bolt itself is E7's to
        // rebuild.
        (void)sky;
        VFX_ComposeImpactPackage(ground, (Vector3){0.0f, 1.0f, 0.0f},
                                 VC_MAT_LIGHTNING, 0.7f, 0.40f);
    }
}

static void LoiDongDraw(Vector3 center, float t, float power) {
    Color c = ELEMENT_COLOR_TAIJI;
    float pulse = 0.85f + 0.15f * sinf(t * 4.0f);
    Vector3 p = { center.x, 0.03f, center.z };
    // Nested rotating rune rings; the resonant version gains a third ring.
    DrawCircle3D(p, LOI_RADIUS * pulse, (Vector3){ 1, 0, 0 }, 90.0f, c);
    DrawCircle3D(p, LOI_RADIUS * 0.62f, (Vector3){ 1, 0, 0 }, 90.0f + t * 40.0f, c);
    if (power > 1.0f) {
        DrawCircle3D(p, LOI_RADIUS * 0.35f, (Vector3){ 1, 0, 0 }, 90.0f - t * 60.0f, c);
    }
}

const FormationDef FORMATION_CUU_THIEN_LOI_DONG = {
    .name = "CUU_THIEN_LOI_DONG",
    .elem = ELEM_METAL, // Lôi rides the Kim identity (cyan/violet dual, see art direction)
    .radius = LOI_RADIUS,
    .duration = LOI_DURATION,
    .manaCost = LOI_MANA_COST,
    .resonantZone = NAT_RIVER, // Lôi Động Trận + Sông (thiết kế)
    .onTick = LoiDongTick,
    .drawGround = LoiDongDraw,
};
