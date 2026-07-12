// boss/boss_system.h
// Boss Đại Tinh Linh (MODULES_ROADMAP.md Module 5). Two halves:
//   ENGINE (boss_system.c, written once): spawn into the shared agent pool
//   as ARCH_BOSS, phase state machine driven by %HP, target picking via
//   Entity_GetNearbyTargetsTeam, skill casts through core/skill_manager
//   (whose projectile skills feed the combat/ registry).
//   DATA (boss/<ten_boss>_def.c, one per boss — where AI creates new
//   bosses): a BossDef table + a drawVisual callback. ONLY the _def.c files
//   may include core VFX headers; the engine stays pure logic.
// BOSS_API.md will document the contract.
#ifndef BOSS_SYSTEM_H
#define BOSS_SYSTEM_H

#include "entities/entities.h"
#include "combat/combat.h"

#define BOSS_MAX_PHASES 4

typedef struct BossDef {
    const char   *name;
    float         maxHealth;
    // Phase i becomes active once health/maxHealth <= phaseHpThresholds[i];
    // [0] must be 1.0f (phase 0 from full HP). Thresholds descend.
    float         phaseHpThresholds[BOSS_MAX_PHASES];
    // Hắc Diện biến hệ: the boss's element per phase (drives clash matrix
    // AND the visual cue — rune color must change with it).
    CombatElement phaseElements[BOSS_MAX_PHASES];
    // Registered skill NAME cast in each phase (resolved once at Boss_Spawn
    // via Skill_GetIndexByName; unresolved names simply don't cast).
    const char   *phaseSkillNames[BOSS_MAX_PHASES];
    float         castIntervalSeconds; // cadence of AI casts
    // Render callback — the ONLY half allowed to touch VFX/raylib drawing.
    // phaseT = seconds since the current phase started.
    void        (*drawVisual)(const Agent *self, float phaseT);
} BossDef;

// Spawns the (single) active boss. Returns its agentId, or -1 (pool full /
// bad def). team is the boss's own side (Phase 0: TEAM_ENEMY vs the player).
int  Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team);

// Phase transitions + AI: picks the nearest opposing-team agent in range,
// casts the current phase's skill on cooldown, updates the agent's element
// on phase change. At <30% HP requests Thái Cực (Module 6). No-op when no
// boss is alive.
void Boss_Update(float dt);

// Calls def->drawVisual for the live boss (inside BeginMode3D).
void Boss_Draw(void);

// Introspection (HUD / autotest). Phase is 0-based; -1 when no live boss.
int  Boss_GetPhase(void);
int  Boss_GetAgentId(void);
bool Boss_IsAlive(void);

// --- Boss defs (one extern per boss/<name>_def.c data file) ---
extern const BossDef BOSS_HAC_DIEN_TON_GIA;

#endif // BOSS_SYSTEM_H
