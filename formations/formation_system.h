// formations/formation_system.h
// Trận Pháp (MODULES_ROADMAP.md Module 10) — ground formations that tick
// modifiers onto agents inside their radius (Tầng 4 of the interaction
// stack). Same engine/data split as boss/:
//   ENGINE (formation_system.c, written once): pool of 4, mana-gated
//   deploy, duration/tick bookkeeping, zone resonance via Map_QueryZoneAt.
//   DATA (formations/<ten_tran>_def.c, one per formation — the AI-creates-
//   formations surface): a FormationDef + onTick (logic, Entity_* only) +
//   drawGround (VFX — only _def.c files may include VFX headers).
// FORMATIONS_API.md will document the contract.
#ifndef FORMATION_SYSTEM_H
#define FORMATION_SYSTEM_H

#include "entities/entities.h"
#include "combat/combat.h"     // CombatElement
#include "core/map_manager.h"  // NatureZoneType (resonance)

#define MAX_FORMATIONS 4

typedef struct FormationDef {
    const char    *name;
    CombatElement  elem;
    float          radius;
    float          duration;   // seconds the formation stays deployed
    float          manaCost;   // charged to the deployer (Entity_TrySpendMana)
    // Đặt đè lên zone này → power 1.5 thay vì 1.0 (Trận Pháp cộng hưởng —
    // e.g. Lôi Động Trận trên Sông). NAT_NONE = no resonance possible.
    NatureZoneType resonantZone;
    // Gameplay tick — Entity_* calls only, no VFX. power is the resonance
    // multiplier (1.0 normal / 1.5 on the resonant zone); ownerTeam scopes
    // buffs vs debuffs.
    void (*onTick)(Vector3 center, float dt, float power, AgentTeam ownerTeam);
    // Ground visual — decals/primitives/VFX composes. t = seconds since
    // deploy. Called inside BeginMode3D.
    void (*drawGround)(Vector3 center, float t, float power);
} FormationDef;

// Deploys into a free pool slot. Charges def->manaCost to ownerAgentId
// (fails when mana is short or the pool is full). Resonance is decided
// ONCE at deploy time from the zone under center. Returns slot id or -1.
int  Formation_Deploy(const FormationDef *def, Vector3 center, int ownerAgentId);
void Formation_Update(float dt);
void Formation_Draw(void);   // inside BeginMode3D
int  Formation_GetActiveCount(void);

// --- Formation defs (one extern per formations/<name>_def.c) ---
extern const FormationDef FORMATION_CUU_THIEN_LOI_DONG; // stun pulses, resonates with NAT_RIVER
extern const FormationDef FORMATION_HAN_BANG_THUY_TUYET; // slow field, resonates with NAT_RIVER

#endif // FORMATION_SYSTEM_H
