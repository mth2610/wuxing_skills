// entities/entities.h
// Entities module — Agent pool, vertical physics (jump/dash/ring-out),
// damage entry point. See ENTITIES_API.md for full spec.
#ifndef ENTITIES_H
#define ENTITIES_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    AGENT_GROUNDED,
    AGENT_JUMPING,
    AGENT_RING_OUT_FALLING
} AgentVerticalState;

// --- Team / archetype (Module 1, MODULES_ROADMAP.md) ---
// TEAM_NEUTRAL agents are hit by everyone's AoE and hit everyone but other
// neutrals — it doubles as the "no team filtering" legacy value.
typedef enum { TEAM_ALLY = 0, TEAM_ENEMY, TEAM_NEUTRAL } AgentTeam;
// One mixed pool (MAX_AGENTS) for heroes/minions/bosses — no per-archetype
// pools; archetype is a tag consumed by ai/boss/game layers, not entities.
typedef enum { ARCH_HERO = 0, ARCH_MINION, ARCH_BOSS } AgentArchetype;

// Equipped-skill loadout size: 5 Strike slots [0..4] + 2 Buff/Support slots [5..6]
#define AGENT_SKILL_SLOTS  7
#define AGENT_STRIKE_SLOTS 5
#define AGENT_BUFF_SLOTS   2

#define MAX_AGENT_MODIFIERS 4

// Status effects (DoTs & combat debuffs — see VO_HOC_BALANCE.md §4.2)
typedef enum {
    STATUS_BURN = 0,       // Bốc hỏa: DoT damage/s (max 3 stacks)
    STATUS_POISON,         // Dính độc: DoT damage/s + 40% healing reduction
    STATUS_BLEED,          // Chảy máu: DoT damage/s
    STATUS_VULNERABLE,     // Phá giáp: +potency fraction damage taken
    STATUS_WEAKEN,         // Suy yếu: -potency fraction damage dealt
    STATUS_TYPE_COUNT
} AgentStatusType;

typedef struct {
    float potency;         // DPS for DoT, or fraction (e.g. 0.25f) for Vuln/Weaken
    float duration;        // seconds remaining; <= 0 inactive
    int   stacks;          // 1..3 for stackable DoTs
    int   attackerId;      // source agentId (-1 if none)
} AgentStatusSlot;

// PvE-only gear/attribute bonuses (disabled / normalized to 0 in PvP modes).
typedef enum {
    PK_MODE_PEACE = 0,     // Hòa Bình: Không đánh Hero khác, không bị người chơi Tỉ Thí đánh
    PK_MODE_SPAR,          // Tỉ Thí: Đấu giao hữu 1v1, kẹp máu ở 1.0 HP (không chết)
    PK_MODE_SLAUGHTER      // Đồ Sát: Tự do tấn công mọi Hero ngoài Safe Zone, tích Sát Khí
} AgentPkMode;

typedef struct {
    float bonusMaxHealth;   // Added to base maxHealth
    float bonusMaxMana;     // Added to base maxMana
    float moveSpeedMult;    // >= 0; <= 0 treated as 1.0f
    float damageOutMult;    // >= 0; <= 0 treated as 1.0f
    float damageTakenMult;  // >= 0; <= 0 treated as 1.0f
    float manaRegenMult;    // >= 0; <= 0 treated as 1.0f
} AgentPvEBonuses;

// Minimal duration-based modifier slot (e.g. Buff speed multiplier).
// speedMult: 1.0 = normal speed; multiplicative; <=0 or unset slot ignored.
// duration: seconds remaining; <=0 means inactive/empty slot.
// NOTE: speedMult is NOT wired into any movement code yet — there is no
// movement system in this minimal version. This is data + tick-down only.
typedef struct {
    float speedMult;
    float duration;
} AgentModifier;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float   health;
    float   maxHealth;
    int     currentElement;      // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    AgentVerticalState vState;
    float   dashCooldown;        // seconds remaining
    bool    isStealthed;         // reserved, not consumed yet
    bool    active;
    AgentModifier modifiers[MAX_AGENT_MODIFIERS];

    // --- Crowd control (see §12 in ENTITIES_API.md) ---
    float   stunTimer;    // seconds remaining; <=0 = not stunned.
    float   freezeTimer;  // seconds remaining; <=0 = not frozen (Hard CC, subject to DR)
    float   rootTimer;    // seconds remaining; <=0 = not rooted (Soft CC, blocks movement)
    float   silenceTimer; // seconds remaining; <=0 = not silenced (Soft CC, blocks casting)
    int     ccDrCount;    // Hard-CC diminishing returns count within 4s window
    float   ccDrTimer;    // 4s DR decay window
    Vector3 pullTarget;   // world position being pulled toward
    float   pullTimer;    // seconds remaining; <=0 = not being pulled
    float   pullSpeed;    // units/sec toward pullTarget, XZ-plane only

    // --- Shield, Status Effects (DoT/Debuff) & Elemental Marks ---
    float           shield;
    float           shieldTimer;
    AgentStatusSlot statuses[STATUS_TYPE_COUNT];
    int             marks[5];         // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    float           markTimer[5];
    float           markImmune[5];

    // --- Mana (see §13 in ENTITIES_API.md) ---
    float   mana;
    float   maxMana;

    // --- Stamina (run vs exhausted walk) & Khinh Công double-jump ---
    float   stamina;
    float   maxStamina;
    bool    staminaExhausted;
    bool    isRunning;
    int     airJumpsUsed;

    // --- Team / archetype (Module 1) ---
    AgentTeam      team;
    AgentArchetype archetype;

    // --- Thiền Định (meditate): immobile, fast mana regen, cancels on
    // damage / jump / dash / launch / stun / pull / actual movement ---
    bool    isMeditating;
    float   meditateTimer;   // seconds remaining; <=0 = not meditating

    // --- Vô Hệ loadout: equipped skill ids + their elements, set via
    // Entity_SetEquippedSkill. skillId < 0 = empty slot. Elements follow
    // the currentElement convention (0..4). ---
    int     equippedSkills[AGENT_SKILL_SLOTS];
    int     equippedElements[AGENT_SKILL_SLOTS];

    // --- Dash state (real impl — horizontal burst integrated in
    // Entity_Update while dashTimer > 0) ---
    Vector3 dashVelocity;
    float   dashTimer;       // seconds remaining of the burst

    // --- Cảnh Giới Thái Cực (Module 6) ---
    // True while in the Taiji state: immune to elemental counters (combat/
    // reads this in the Clash Matrix), normal skill slots locked, Phong/Lôi
    // unlocked (control/ reads this). Auto-exits when mana hits 0
    // (Entity_Update). Entered via a balanced 2 Âm + 2 Dương loadout
    // (Entity_RecomputeElement) or forced by boss/ (<30% HP).
    bool    taijiActive;

    // --- PvE-only gear bonuses (zeroed / inactive in PvP) ---
    float          baseMaxHealth;
    float          baseMaxMana;
    bool           pveBonusesActive;
    AgentPvEBonuses pveBonuses;

    // --- Open-world PK & Skill-Mastery tracking ---
    bool           duelFlag;            // Legacy/sync flag: true when pkMode != PK_MODE_PEACE
    AgentPkMode    pkMode;              // 3-state open-world PK stance
    int            infamyLevel;         // Slaughter infamy (Sát Khí) stack count
    bool           sparDefeated;        // Set when HP hits 1.0f floor during non-lethal SPAR
    int            lastAttackerId;      // Last Hero/Agent who damaged this agent
    bool           inSafeZone;          // True inside Safe Sanctum (e.g. Oasis) — immune to PK
    bool           isStatic;            // Immobile prop/dummy agent (e.g. Training Stele): no AI march, no knockback, no minion render
    int            lastCastSkillIndex;  // Last skill cast by this agent (for direct use-based XP)
    float          lastCastDamageScale; // PvE skill-level damage multiplier (ignored vs ARCH_HERO)
    float          hitFlashTimer;       // Brief white hit-flash feedback (seconds remaining)
    float          dodgeGraceTimer;     // I-frame grace window after Dash/Jump (seconds remaining)
    float          facingYaw;           // World facing angle in radians (replicated in 18B snapshot)
    unsigned char  netMonsterMeta;      // Packed [bits 0..1 kind | bits 2..4 aiState | bits 5..7 telegraph]
} Agent;

// Sized for the real target scale (up to 6 real players + a mixed pool of
// minions/mid-tier monsters/2 bosses), not just the original 4v4 test
// scaffolding — static array, cost is ~20KB total, negligible. Bump further
// if a future design ever needs more; this is a compile-time ceiling only,
// actual live agent count still varies freely under it at runtime.
#define MAX_AGENTS 256

// --- Lifecycle ---
void Entity_Init(void);
void Entity_Update(float dt);
void Entity_Draw(void);
void Entity_Unload(void);

// --- Damage ---
void Entity_ApplyDamage(int agentId, float damage, Vector3 knockback);
void Entity_ApplyDamageFrom(int targetId, int attackerId, float baseDamage,
                            float skillScale, Vector3 knockback);
bool Entity_CanDamageTarget(int attackerId, int targetId);
void Entity_SetDuelFlag(int agentId, bool on);
void Entity_SetStatic(int agentId, bool isStatic);
bool Entity_GetDuelFlag(int agentId);
void Entity_SetPkMode(int agentId, AgentPkMode mode);
AgentPkMode Entity_GetPkMode(int agentId);
void Entity_AddInfamy(int agentId, int delta);
int  Entity_GetInfamyLevel(int agentId);
bool Entity_ConsumeSparDefeated(int agentId, int *outAttackerId);
void Entity_SetSafeZone(int agentId, bool inSafe);
bool Entity_IsInSafeZone(int agentId);
void Entity_SetLastCast(int agentId, int skillIndex, float pveDamageScale);

// --- Vertical physics ---
void Entity_Jump(int agentId, float force);
bool Entity_PerformJump(int agentId, float verticalForce, Vector3 horizontalVelocity);
void Entity_SetRunning(int agentId, bool running);
bool Entity_IsRunning(int agentId);
void Entity_Dash(int agentId, Vector3 direction, float speed);
void Entity_ArmDodgeGrace(int agentId, float duration);
bool Entity_CheckRingOut(int agentId);

// Ring-out bounds are PER-MAP now (default: DEFAULT_ARENA's circle, center
// (6,0,4.4) r=18 — matching MAP_API.md §3). The match screen sets them when
// it pins its map (e.g. VERDANT_PATH's plateau: center (50,0,37.5) r=34)
// and main.c restores the default on the way back to the sandbox. radius
// <= 0 is ignored.
void Entity_SetArenaBounds(Vector3 center, float radius);
// Read them back (ai/'s hero-bot edge guard, HUD debug).
void Entity_GetArenaBounds(Vector3 *outCenter, float *outRadius);

// Optional rectangular playable bounds for Open World DEM maps (overrides circular ring-out when active).
void Entity_SetPlayableRect(float minX, float maxX, float minZ, float maxZ);
void Entity_ClearPlayableRect(void);

// Rebuilds internal 2D XZ spatial hash grid (also auto-rebuilt lazily when dirty).
void Entity_RebuildSpatialGrid(void);

// --- Crowd control (see ENTITIES_API.md §12) ---
// Launch: sets velocity and vState = AGENT_JUMPING (shared with Entity_Jump).
// Entity_Update integrates gravity + full 3-axis position while airborne and
// lands back to AGENT_GROUNDED at position.y <= 0 — a real ballistic arc, not
// just a vertical pop.
void Entity_ApplyLaunch(int agentId, float verticalForce, Vector3 horizontalVelocity);
// Stun: sets/refreshes stunTimer. Entities has no action/input system to
// enforce this — callers (skill cast, input) must check Entity_IsStunned.
void Entity_ApplyStun(int agentId, float duration);
bool Entity_IsStunned(int agentId);
// Pull: entities directly integrates position.x/z toward targetPos at speed
// for duration seconds (self-contained, does not rely on velocity/external
// integration — see ENTITIES_API.md §12 for why).
void Entity_ApplyPull(int agentId, Vector3 targetPos, float speed, float duration);
// Convenience combined read for external movement/input systems deciding
// whether to skip their own position writes this frame.
bool Entity_IsCrowdControlled(int agentId);

// --- Extended CC, Shield, Heal, Status & Elemental Marks (VO_HOC_BALANCE.md) ---
void  Entity_ApplyFreeze(int agentId, float duration);
bool  Entity_IsFrozen(int agentId);
void  Entity_ApplyRoot(int agentId, float duration);
bool  Entity_IsRooted(int agentId);
void  Entity_ApplySilence(int agentId, float duration);
bool  Entity_IsSilenced(int agentId);
void  Entity_Cleanse(int agentId);

void  Entity_AddShield(int agentId, float amount, float duration);
float Entity_GetShield(int agentId);
void  Entity_Heal(int agentId, float amount);

void  Entity_ApplyStatus(int agentId, AgentStatusType type, float potency,
                         float duration, int attackerId);
bool  Entity_HasStatus(int agentId, AgentStatusType type);

int   Entity_AddMark(int agentId, int element, int stacks, int maxStacks, float duration);
int   Entity_GetMarkStacks(int agentId, int element);
int   Entity_ConsumeMarks(int agentId, int element);
void  Entity_SetMarkImmune(int agentId, int element, float duration);

// --- Mana (see ENTITIES_API.md §13) ---
// Spawned agents get mana = maxMana = 100.0f by default (Entity_SpawnAgent).
// Passive regen (5/sec) ticks in Entity_Update, capped at maxMana.
// Returns false and leaves mana untouched if amount > current mana.
bool Entity_TrySpendMana(int agentId, float amount);
void Entity_RestoreMana(int agentId, float amount);

// --- Basic attack (see ENTITIES_API.md §13) ---
// Separate from the CastSkill/skill pipeline entirely — free, no cast time,
// no cooldown gate, spammable. Auto-targets: internally scans
// Entity_GetNearbyTargets around the attacker (no targetId parameter — no
// "enemy" reference needed) and picks the nearest active agent other than
// the attacker. No team filtering yet (Agent has no team field — known gap,
// see entities/CLAUDE.md's Module 1 note). Returns false immediately with no
// side effects if no other agent is within AUTO_TARGET_RADIUS.
// Applies damage/knockback on the auto-found target (PALM also applies a
// small Entity_ApplyLaunch pop) and reports its position via outTargetPos
// (caller has no targetId to look this up itself — needed for VFX, e.g. a
// wall-bonus beam's endpoint). If the attacker is standing near a
// registered wall (core/skill_manager.h's SkillManager_FindNearbyWall), also
// applies a small bonus hit and reports the wall's position/element via
// out-params so the caller (which owns VFX — entities does not call
// rendering code) can spawn the matching elemental bonus effect. Returns
// true iff a wall bonus fired (independent of whether a target was found —
// wall bonus still needs a target to hit, see entities.c).
typedef enum { BASIC_ATTACK_PUNCH, BASIC_ATTACK_KICK, BASIC_ATTACK_PALM } BasicAttackType;
bool Entity_ExecuteBasicAttack(int attackerId, BasicAttackType type,
                               Vector3 *outTargetPos,
                               Vector3 *outWallPos, int *outWallElement);

// How long this attack's swing takes, in seconds — gameplay data (single
// source of truth for both game/ and sandbox/ callers), consumed by the
// rendering side (character/character_model.h's
// CharacterModel_TriggerAttackTimed) to pace the swing animation. Escalates
// with the move's weight: punch < kick < palm.
float Entity_GetBasicAttackSeconds(BasicAttackType type);

// --- Dash/afterimage hook (stub) ---
void Entity_OnDash(int agentId);

// --- Thiền Định (Module 1) ---
// Starts a 3s meditate: agent must be active, grounded and not crowd-
// controlled. While meditating mana regenerates fast enough to refill a
// default 100-mana pool from empty within the 3s. Cancelled automatically by
// damage, jump, dash, launch, stun, pull, or an actual position change
// through Entity_SetPosition. No-op if preconditions fail.
void Entity_StartMeditate(int agentId);
bool Entity_IsMeditating(int agentId);
// Channel fraction while meditating: 0 at start → 1 near natural finish.
// Returns 0 if not meditating / invalid agent. For VFX consumers only.
float Entity_GetMeditateProgress(int agentId);

// --- Vô Hệ loadout (Module 1) ---
// Writes skillId+element into the given slot (0..AGENT_SKILL_SLOTS-1) and
// immediately recomputes currentElement. skillId < 0 clears the slot.
// element uses the same 0..4 convention as Agent.currentElement; entities
// deliberately does not know the skill registry — the caller (control/game)
// passes the element alongside the id.
void Entity_SetEquippedSkill(int agentId, int slot, int skillId, int element);
// Majority element across non-empty equipped slots (tie → lowest element
// index; no non-empty slots → currentElement unchanged). Writes the result
// into currentElement and returns it (-1 if agentId invalid/inactive).
int Entity_RecomputeElement(int agentId);

// Direct element override (boss/ phase transitions — Hắc Diện biến hệ).
// Loadout-based agents should use Entity_SetEquippedSkill/RecomputeElement
// instead; a later RecomputeElement overwrites this.
void Entity_SetElement(int agentId, int element);

// Runtime team reassignment (lobby: host moves a player between sides
// before the match — net/'s room management is the caller).
void Entity_SetAgentTeam(int agentId, AgentTeam team);

// Scale max HP (current HP scales along, floor 1). Team-battle handicap
// buff (Đợt A4): game/ applies GameRules_HandicapFor to the short side at
// round start — mult > 1 only; this is not a general stat system.
void Entity_ScaleMaxHealth(int agentId, float mult);

// PvE vs PvP normalization: passing non-NULL activates gear bonuses for PvE
// (scaling maxHealth/maxMana/speed/damage/regen); passing NULL resets to
// normalized PvP base stats (100% fair-play).
void  Entity_SetPvEBonuses(int agentId, const AgentPvEBonuses *bonuses);
float Entity_GetDamageOutMult(int agentId);
float Entity_GetDamageTakenMult(int agentId);

// --- Thái Cực (Module 6) ---
// Force the state on/off (boss/ at <30% HP; Entity_RecomputeElement sets it
// automatically for a balanced 2 Âm (Thủy/Mộc) + 2 Dương (Hỏa/Kim) loadout).
// While active, running out of mana exits the state automatically.
void Entity_SetTaijiActive(int agentId, bool active);
bool Entity_IsTaijiActive(int agentId);

// Stealth flag setter (zone rule "Mộc ẩn hình trong Rừng" — game/ applies
// it; auto-targeting/boss AI consume the flag later).
void Entity_SetStealth(int agentId, bool stealthed);

// --- Net snapshot mirroring (net/ transport ONLY — never gameplay code).
// A connected CLIENT mirrors the host's pool 1:1 by host agent id:
// SyncBegin → SyncAgent per snapshot entry (activates/overwrites the slot)
// → SyncEnd (deactivates every active agent the snapshot didn't mention,
// including stale local-only agents from before the connection).
void Entity_NetSyncBegin(void);
void Entity_NetSyncAgent(int agentId, Vector3 pos, float health, float maxHealth,
                         float mana, float maxMana, int element,
                         AgentTeam team, AgentArchetype archetype,
                         bool taiji, bool meditating, bool stealthed, bool duelFlag);
void Entity_NetSyncEnd(void);
void Entity_SetFacingYaw(int agentId, float yawRadians);
void Entity_SetNetMonsterMeta(int agentId, unsigned char packedMeta);
void Entity_TickClientVisuals(float dt);

// --- Speed multiplier read (Module 1) ---
// Product of all active modifier slots' speedMult (>0 && duration>0).
// Returns 1.0 for no active modifiers or invalid/inactive agent. External
// movement code (control/, sandbox) multiplies its move speed by this.
float Entity_GetSpeedMult(int agentId);

// --- Nearby target query (pure read, no side effects) ---
// Returns the number of active agents found within radius of center, writing
// their indices into outIds (caller-supplied buffer, size maxIds).
// XZ-plane distance check (ignore Y), matching arena/ring-out checks.
int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds);

// Team-filtered variant: only agents whose team == filter are returned.
int Entity_GetNearbyTargetsTeam(Vector3 center, float radius, AgentTeam filter,
                                int *outIds, int maxIds);

// --- Buff modifier setter ---
// Finds an empty/expired slot in agentPool[agentId].modifiers[] and writes
// into it (simple find-first-empty, no priority/stacking logic).
void Entity_AddModifier(int agentId, float speedMult, float duration);

// --- AoE composition entry points (built on Entity_GetNearbyTargets) ---
// Applies damage + knockback (away from center) to every active agent found
// within radius whose team != attackerTeam (fixes the old "hits everyone"
// limitation). Pass TEAM_NEUTRAL as attackerTeam to hit both real teams.
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage,
                           float knockbackStrength, AgentTeam attackerTeam);
// Applies a buff modifier only to agents whose team == allyTeam.
void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult,
                         float duration, AgentTeam allyTeam);

// --- Spawn / read-only access ---
// Finds the first inactive slot in agentPool, initializes it, returns its id
// (0..MAX_AGENTS-1), or -1 if the pool is full. Equipped slots start empty
// (skillId -1); currentElement stays the passed element until a loadout is
// set via Entity_SetEquippedSkill.
int Entity_SpawnAgent(Vector3 position, float maxHealth, int element,
                      AgentTeam team, AgentArchetype archetype);

// Read-only accessor. Returns NULL if agentId is out of range or the slot is
// inactive — caller must NULL-check, do not assume a valid pointer. This is
// the only sanctioned way for other modules to read agent state; mutation
// must go through the existing Entity_* setter functions.
const Agent *Entity_GetAgent(int agentId);

// --- Position sync (for external movement systems) ---
// Overwrites agentId's position directly. Entities module owns no horizontal
// movement system — callers (e.g. sandbox/skills) that compute their own
// X/Z movement must push the result here each frame so Entity_GetNearbyTargets
// and other position-based queries stay accurate. Does nothing if agentId is
// out of range or inactive.
void Entity_SetPosition(int agentId, Vector3 position);

#endif // ENTITIES_H
