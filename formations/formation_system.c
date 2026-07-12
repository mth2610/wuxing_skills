// formations/formation_system.c — ENGINE half: written once; new formations
// are new <ten_tran>_def.c data files. Pure bookkeeping — no VFX here.
#include "formations/formation_system.h"
#include <stddef.h>

static const float RESONANT_POWER = 1.5f; // Trận Pháp cộng hưởng multiplier

typedef struct {
    const FormationDef *def;
    Vector3   center;
    float     timeLeft;
    float     t;         // seconds since deploy (drawGround clock)
    float     power;     // 1.0 / RESONANT_POWER, decided at deploy
    AgentTeam ownerTeam;
    bool      active;
} FormationSlot;

static FormationSlot s_pool[MAX_FORMATIONS];

int Formation_Deploy(const FormationDef *def, Vector3 center, int ownerAgentId) {
    if (def == NULL) return -1;

    const Agent *owner = Entity_GetAgent(ownerAgentId);
    if (owner == NULL) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_FORMATIONS; i++) {
        if (!s_pool[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1; // formationPool[4] full (thiết kế §VI)

    // Mana gate before anything sticks.
    if (def->manaCost > 0.0f && !Entity_TrySpendMana(ownerAgentId, def->manaCost)) {
        return -1;
    }

    // Resonance decided once at deploy: formation đè lên đúng zone → mạnh hơn.
    float power = 1.0f;
    if (def->resonantZone != NAT_NONE && Map_QueryZoneAt(center) == def->resonantZone) {
        power = RESONANT_POWER;
    }

    s_pool[slot] = (FormationSlot){
        .def = def,
        .center = (Vector3){ center.x, 0.0f, center.z },
        .timeLeft = def->duration,
        .t = 0.0f,
        .power = power,
        .ownerTeam = owner->team,
        .active = true,
    };
    return slot;
}

void Formation_Update(float dt) {
    for (int i = 0; i < MAX_FORMATIONS; i++) {
        FormationSlot *f = &s_pool[i];
        if (!f->active) continue;
        f->t += dt;
        f->timeLeft -= dt;
        if (f->def->onTick) f->def->onTick(f->center, dt, f->power, f->ownerTeam);
        if (f->timeLeft <= 0.0f) f->active = false;
    }
}

void Formation_Draw(void) {
    for (int i = 0; i < MAX_FORMATIONS; i++) {
        FormationSlot *f = &s_pool[i];
        if (!f->active || f->def->drawGround == NULL) continue;
        f->def->drawGround(f->center, f->t, f->power);
    }
}

int Formation_GetActiveCount(void) {
    int n = 0;
    for (int i = 0; i < MAX_FORMATIONS; i++) n += s_pool[i].active ? 1 : 0;
    return n;
}
