#ifndef SKILL_MANAGER_H
#define SKILL_MANAGER_H

#include "raylib.h"
#include "core/tuning.h"
#include "core/skill_curve.h"
#include <stdbool.h>
#include "core/camera_context.h"

// Base colors for Wuxing and Taiji elements
#define ELEMENT_COLOR_WATER (Color){ 41, 128, 185, 255 }  // Cyan-Blue
#define ELEMENT_COLOR_WOOD  (Color){ 46, 204, 113, 255 }  // Vibrant Green
#define ELEMENT_COLOR_FIRE  (Color){ 231, 76, 60, 255 }   // Crimson Red
#define ELEMENT_COLOR_EARTH (Color){ 230, 126, 34, 255 }  // Ochre Brown / Orange
#define ELEMENT_COLOR_METAL (Color){ 149, 165, 166, 255 } // Silver Gray
#define ELEMENT_COLOR_TAIJI (Color){ 155, 89, 182, 255 } // Purple

typedef enum {
    SKILL_WATER = 0,
    SKILL_METAL,
    SKILL_FIRE,
    SKILL_WOOD,
    SKILL_ELECTRIC
} SkillType;

typedef enum {
    CAST_ANCHOR_CASTER = 0,
    CAST_ANCHOR_TARGET
} CastAnchorType;

typedef enum {
    CAST_PATH_PROJECTILE = 0,
    CAST_PATH_UNDERGROUND,
    CAST_PATH_SKY
} CastPathType;

typedef enum {
    SKILL_CAT_PROJECTILE = 0, // Tầm xa: Sát thương thấp, bay nhanh, an toàn cao
    SKILL_CAT_AOE_CONTROL,    // Tầm trung: Sát thương vừa, khống chế mạnh (Slow/Root), diện rộng, hồi lâu
    SKILL_CAT_MELEE,          // Cận chiến: Sát thương cực cao, phá giáp, đẩy lùi mạnh, tầm ngắn
    SKILL_CAT_TRAP_UTILITY,   // Bùa chú / Trận pháp: Ném cắm cọc, hiệu ứng dị biệt, hồi rất lâu
    SKILL_CAT_BUFF_SUPPORT    // Hỗ trợ / Hộ thuẫn: Tạo khiên chặn đạn, tăng tốc, mana, không gây dame
} SkillCategory;

typedef struct {
    int level;
    int milestone;
    int quantity;
    float sizeScale;
    float damage;
    CastAnchorType anchorType;
    CastPathType pathType;
    bool showPortal;
    
    // Path Drawing Data
    int pathPointCount;
    Vector3 pathPoints[32]; // Max 32 points for a drag-to-cast path
} SkillParams;

float Skill_CalculateDamage(SkillCategory cat, SkillParams params);
float Skill_CalculateCooldown(SkillCategory cat, SkillParams params);
float Skill_CalculateKnockback(SkillCategory cat, SkillParams params);
float Skill_CalculateManaCost(SkillCategory cat, SkillParams params);
const char* Skill_GetCategoryName(SkillCategory cat);

void InitSkillManager(int screenWidth, int screenHeight);
void UpdateSkillManager(float dt, Vector3 enemyPos, float enemyRadius);
void DrawSkillManagerWorld3D(void);
void DrawSkillManagerOverlay(void);
void UnloadSkillManager(void);
// agentId: caster's entities/entities.h agent pool slot — forwarded straight
// into skillRegistry[skillIndex].cast(agentId, ...) (CORE_ISSUES.md Item 15).
// Not auto-enforced for cooldown/abort: callers that want gating must still
// call SkillManager_CanCast/TriggerCooldown themselves around this call.
// Pass any stable int if the caster has no real agentId yet.
void CastSkill(int skillIndex, int agentId, Vector3 startPos, Vector3 target, SkillParams params);

int RegisterSkill(const char* name, Color color,
                  void (*init)(int screenWidth, int screenHeight),
                  void (*cast)(int agentId, Vector3 startPos, Vector3 target, SkillParams params),
                  void (*update)(float dt, Vector3 enemyPos, float enemyRadius),
                  void (*draw)(void),
                  void (*unload)(void));
void SetSkillOverrides(int skillIndex, int pathType, int anchorType, int quantity, float sizeScale);
int GetRegisteredSkillCount(void);
const char* GetRegisteredSkillName(int index);
Color GetRegisteredSkillColor(int index);

// Reverse lookup — lets a skill find its own registered skillIndex (the
// RegisterSkill() return value isn't captured anywhere by convention today,
// which is why no skill has adopted SkillManager_CanCast/TriggerCooldown
// yet — see CORE_ISSUES.md Item 15's "New minor gap surfaced" note). Call
// once in Init[Name]Skill and cache the result in a static; -1 if no
// registered skill matches `name` exactly (same name string passed to your
// own RegisterSkill() call).
int Skill_GetIndexByName(const char *name);

typedef struct {
    Vector2 screenPos;
    float   depthFactor;
    bool    behindCamera;
} ProjectedPoint;

ProjectedPoint ProjectPointCached(Vector3 worldPos, Camera3D cam);

void RegisterStaticOccluder(Vector3 center, float radius, float height);
void ClearStaticOccluders(void);
float GetLineOfSightVisibility(Vector3 viewPoint, Vector3 targetPoint);

bool IsAnySkillCoiling(void);
bool IsAnySkillShocking(void);
bool IsEnemySlowed(void);
bool IsEnemyBurning(void);
bool IsEnemyRooted(void);

void AddRootToEnemy(float duration);
void AddSlowToEnemy(float duration);
void AddFloatingText(Vector3 pos, const char *text, Color color, float size, float lifetime);

Vector3 GetAccumulatedKnockback(void);
void ClearAccumulatedKnockback(void);
void AddKnockbackToEnemy(Vector3 force);

// New Combat and Shader API functions for Core API test
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);

// Cooldown/resource GATING STATE (CORE_ISSUES.md Item 16). Skill_Calculate*
// above only compute numbers; these two track real elapsed-time state so a
// skill can actually be prevented from firing back-to-back. Keyed by
// (skillIndex, agentId) — agentId is the caster's entities/entities.h agent
// pool slot (see PlayerEntity/EnemyEntity's `agentId` field). Each caster
// gets an independent cooldown per skill; casting Fireball as agent 3 does
// not lock out agent 7's Fireball. Pass any stable int if a caster has no
// real agentId yet (e.g. 0) — just be consistent per-caster.
bool SkillManager_CanCast(int skillIndex, int agentId);
void SkillManager_TriggerCooldown(int skillIndex, int agentId, float cooldownSeconds);

// Optional abort/interrupt registration (CORE_ISSUES.md Item 14). A skill may
// call this in addition to RegisterSkill() if it wants to support being
// force-aborted (e.g. by a future crowd-control effect) — purely additive,
// does not change the required skill lifecycle contract. Skills that never
// call this simply can't be force-aborted (AbortSkill becomes a no-op with a
// LOG_WARNING for that index). The callback receives the agentId AbortSkill
// was called with, so a skill tracking per-caster instances (once it adopts
// that pattern) can abort only the matching caster's instance instead of
// every active instance of that skill type; a skill that doesn't track
// per-caster ownership can just ignore the parameter and abort everything,
// identical to the previous behavior.
void RegisterSkillAbort(int skillIndex, void (*abort)(int agentId));
void AbortSkill(int skillIndex, int agentId);

// Optional lifecycle-end query registration (CORE_ISSUES.md Item 13). A
// skill may call this in addition to RegisterSkill() if it wants gameplay
// code to be able to ask "is there still an active instance of this skill
// owned by agentId X" — e.g. an Earth wall / Wood root-zone effect that
// gameplay logic needs to know is truly gone before releasing whatever it's
// blocking. Purely additive, does not change the required lifecycle
// contract. Skills that never call this report "not active" unconditionally
// (safe default — a caller never waits forever on a skill that never opted
// in).
void RegisterSkillLifecycleQuery(int skillIndex, bool (*hasActiveInstance)(int agentId));
bool Skill_HasActiveInstance(int skillIndex, int agentId);

// Optional per-skill tunable-parameter registration. A skill may call this
// in addition to RegisterSkill() to expose its physics/visual magic numbers
// (force strength, speed, radius, lifetime...) as named, min/max-bounded
// sliders in the sandbox UI (sandbox/ui_panel.c) instead of only being
// editable by recompiling. `entries` must point at storage the skill keeps
// alive for its own lifetime (a static array is the usual choice) — the
// registry stores the array by value (copies each entry, including the
// `value` pointer), so the skill's own static float fields keep being the
// single source of truth; sandbox writes through `value` directly when a
// slider is dragged. Purely additive — skills that never call this simply
// have no tunables exposed (Skill_GetTunables returns 0 for them).
#define MAX_SKILL_TUNABLES 200
typedef struct {
    char label[32]; // key used both as the sandbox slider label and the
                     // key= name written to/read from the skill's .tuning file
    float *value;
    float min, max, defaultValue;

    // Optional, appended (existing positional-init call sites like
    // {"label", &val, min, max, def} keep compiling unchanged — these
    // trailing fields zero-init to NULL/0, i.e. today's exact behavior).
    const char *phase; // NULL = ungrouped (legacy). Free-form tag for sandbox
                        // grouping, e.g. "cast"/"fly"/"impact"/"rain" — by
                        // convention matches a LayeredTimeline layer's tag
                        // (Timeline_AddLayer in core/skill_helper.h) when the
                        // skill uses one, but not enforced.
    SkillCurve *curve;  // NULL = plain constant via `value` (legacy). Non-NULL
                         // = curve-kind entry; `value` must be NULL in this
                         // case. The curve's 5 keys are the storage; sandbox
                         // draws 5 sliders (one per keyframe) instead of 1.
                         // Skill reads it fresh each frame via
                         // SkillCurve_Eval(curve, t01) — t01 is caller-defined
                         // progress, never distance-to-target (see
                         // SkillHelper_StepCurveFlight for flight speed).
} SkillTunableEntry;

void RegisterSkillTunables(int skillIndex, const SkillTunableEntry *entries, int count);
int  Skill_GetTunables(int skillIndex, SkillTunableEntry *outEntries, int maxEntries);

// Flatten/unflatten a possibly-curve-kind entry list to/from the plain
// "key = value" text format core/tuning.c already parses — a curve entry
// labeled "foo" becomes 5 ordinary keys "foo_t0".."foo_t4"; a constant entry
// stays a single "foo" key. No changes to tuning.c's format/parser needed.
#define SKILL_TUNABLES_MAX_FLAT_KEYS (MAX_SKILL_TUNABLES * SKILL_CURVE_KEYS)

// Expands entries into outKeys/outValues, pre-filled from each entry's
// CURRENT value(s) — suitable directly as Tuning_SaveFloats's input, or as
// the pre-filled outValues for Tuning_LoadFloatsFromPath (whose missing-key
// semantics require defaults to already be present). Returns the number of
// flat keys written (<= maxKeys).
int SkillTunables_Flatten(const SkillTunableEntry *entries, int count,
                           char outKeys[][TUNING_MAX_KEY_LEN], float *outValues,
                           int maxKeys);

// Inverse of Flatten: scatters a flat key/value list (same "label"/"label_tN"
// key scheme) back into each entry's `value`/`curve->v` storage.
void SkillTunables_Unflatten(const SkillTunableEntry *entries, int count,
                              const char *const *keys, const float *values,
                              int keyCount);

// Convenience: Flatten -> Tuning_LoadFloatsFromPath -> Unflatten in one call,
// so a skill's Init function doesn't need to hand-build a flattened key
// array itself. Call before RegisterSkillTunables, same call-site shape
// skills already use today for Tuning_LoadFloatsFromPath. Returns false if
// the file doesn't exist (entries left at their current/default values).
bool SkillTunables_LoadPersisted(const char *path, SkillTunableEntry *entries, int count);

// Agent position provider — lets skill_manager query agent positions without
// depending on entities.h. Register from Entity_Init.
typedef bool (*AgentPosProviderFn)(int agentId, Vector3 *outPos);
void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos);

// Nearby-targets provider — lets skill_helper build chain effects without
// depending on entities.h. Register from Entity_Init.
typedef int (*NearbyTargetsProviderFn)(Vector3 center, float radius,
                                       int *outIds, int maxIds);
void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
int  SkillManager_GetNearbyTargets(Vector3 center, float radius,
                                   int *outIds, int maxIds);

#endif // SKILL_MANAGER_H
