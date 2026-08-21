#include "sandbox/auto_test_cases.h"

#include "sandbox/auto_test.h"
#include "core/skill_manager.h"
#include "core/material/material_system.h"
#include "core/map_manager.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "game/game_screen.h"
#include "game/game_rules.h"
#include "ai/ai.h"
#include "formations/formation_system.h"
#include "net/net.h"
#include "net/net_transport.h"
#include "ui/ui.h"
#include "skills/taiji/taiji_phong/taiji_phong_skill.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

static PlayerEntity *s_player;

static AutoTestResult AutoTest_SmokeStep(int frameInCase, char *outReason, int outReasonSize) {
  (void)frameInCase;
  return AutoTest_ExpectTrue(GetRegisteredSkillCount() > 0,
                             "skill manager has registered skills",
                             outReason, outReasonSize)
             ? AUTOTEST_PASS
             : AUTOTEST_FAIL;
}

// Module 1 DoD (MODULES_ROADMAP.md): team-filtered AoE/buff, mana gate,
// meditate refill, Vô Hệ majority element, real dash. Self-contained: drives
// Entity_Update with its own dt and kills its test agents when done.
static AutoTestResult AutoTest_EntitiesCombatV2Step(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  // Spawn away from the sandbox spawns (nearest: training dummy at (6,0,8))
  // but inside arena radius (center (6,0,4.4), r=18) so neither ring-out nor
  // the 5m team-query radius picks up strangers.
  Vector3 base = { 6.0f, 0.0f, 14.0f };
  int a = Entity_SpawnAgent(base, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int b = Entity_SpawnAgent((Vector3){ base.x + 1.0f, 0, base.z }, 100.0f, 2, TEAM_ENEMY, ARCH_HERO);
  int c = Entity_SpawnAgent((Vector3){ base.x - 1.0f, 0, base.z }, 100.0f, 1, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(a >= 0 && b >= 0 && c >= 0, "spawned 3 team agents", outReason, outReasonSize);

  // Team-filtered AoE damage: ALLY attack hits only the ENEMY.
  Entity_ApplyAoEDamage(base, 5.0f, 10.0f, 0.0f, TEAM_ALLY);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(b)->health, 90.0f, 0.01f, "AoE hit enemy", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(a)->health, 100.0f, 0.01f, "AoE spared ally A", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(c)->health, 100.0f, 0.01f, "AoE spared ally C", outReason, outReasonSize);

  // Team-filtered buff: only allies get the speed modifier.
  Entity_ApplyAoEBuff(base, 5.0f, 1.5f, 5.0f, TEAM_ALLY);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetSpeedMult(a), 1.5f, 0.01f, "buff hit ally", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetSpeedMult(b), 1.0f, 0.01f, "buff spared enemy", outReason, outReasonSize);

  // Team query.
  int ids[8];
  ok = ok && AutoTest_ExpectTrue(Entity_GetNearbyTargetsTeam(base, 5.0f, TEAM_ENEMY, ids, 8) == 1,
                                 "team query finds 1 enemy", outReason, outReasonSize);

  // Mana gate: drain then over-spend fails.
  ok = ok && AutoTest_ExpectTrue(Entity_TrySpendMana(a, 100.0f), "spend full mana ok", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!Entity_TrySpendMana(a, 50.0f), "over-spend fails when drained", outReason, outReasonSize);

  // Meditate: 3s refills the default pool (drive update manually).
  Entity_StartMeditate(a);
  ok = ok && AutoTest_ExpectTrue(Entity_IsMeditating(a), "meditate started", outReason, outReasonSize);
  for (int i = 0; i < 7; i++) Entity_Update(0.5f);
  ok = ok && AutoTest_ExpectTrue(!Entity_IsMeditating(a), "meditate ended after 3s", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(a)->mana, Entity_GetAgent(a)->maxMana, 0.01f,
                                      "meditate refilled mana", outReason, outReasonSize);

  // Meditate cancels on damage.
  Entity_StartMeditate(b);
  Entity_ApplyDamage(b, 1.0f, (Vector3){ 0 });
  ok = ok && AutoTest_ExpectTrue(!Entity_IsMeditating(b), "damage cancels meditate", outReason, outReasonSize);

  // Vô Hệ: majority element across equipped slots, re-equip flips it.
  Entity_SetEquippedSkill(a, 0, 10, 2); // fire
  Entity_SetEquippedSkill(a, 1, 11, 2); // fire
  Entity_SetEquippedSkill(a, 2, 12, 0); // water
  Entity_SetEquippedSkill(a, 3, 13, 4); // metal
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(a)->currentElement == 2, "majority element = fire", outReason, outReasonSize);
  Entity_SetEquippedSkill(a, 1, 14, 0); // swap a fire for water → water majority
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(a)->currentElement == 0, "re-equip flips to water", outReason, outReasonSize);

  // Real dash: covers ~speed*0.15s horizontally, gated by cooldown.
  float xBefore = Entity_GetAgent(c)->position.x;
  Entity_Dash(c, (Vector3){ 1.0f, 0.0f, 0.0f }, 10.0f);
  for (int i = 0; i < 6; i++) Entity_Update(0.05f);
  float dashDist = Entity_GetAgent(c)->position.x - xBefore;
  ok = ok && AutoTest_ExpectFloatNear(dashDist, 1.5f, 0.3f, "dash moved ~1.5m", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(c)->dashCooldown > 0.0f, "dash set cooldown", outReason, outReasonSize);

  // Cleanup: kill test agents so later cases see a clean pool.
  Entity_ApplyDamage(a, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(b, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(c, 1e9f, (Vector3){ 0 });

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 2 DoD: active map (DEFAULT_ARENA) exposes its nature zones; point
// queries resolve river/forest/desert vs none.
static AutoTestResult AutoTest_MapZonesStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  bool ok = AutoTest_ExpectTrue(Map_GetZoneCount() == 3, "default arena has 3 zones", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Map_QueryZoneAt((Vector3){ 0.0f, 0, -2.0f }) == NAT_RIVER,
                                 "river zone at its center", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Map_QueryZoneAt((Vector3){ 14.0f, 0, 10.0f }) == NAT_FOREST,
                                 "forest zone at its center", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Map_QueryZoneAt((Vector3){ -2.0f, 0, 10.0f }) == NAT_DESERT_ZONE,
                                 "desert zone at its center", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Map_QueryZoneAt((Vector3){ 6.0f, 0, 4.4f }) == NAT_NONE,
                                 "arena center is zone-free", outReason, outReasonSize);
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 3 DoD: Thủy–Hỏa head-on clash → Thủy wins with correct events both
// sides; same-team projectiles pass through silently; projectile→agent hit
// applies damage via combat with a CLASH_HIT_AGENT event; 128-collider cap.
static AutoTestResult AutoTest_CombatClashStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Vector3 base = { 6.0f, 0.0f, -6.0f }; // clear of sandbox spawns + zones
  int ally = Entity_SpawnAgent(base, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int foe  = Entity_SpawnAgent((Vector3){ base.x + 4.0f, 0, base.z }, 100.0f, 2, TEAM_ENEMY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(ally >= 0 && foe >= 0, "spawned clash agents", outReason, outReasonSize);

  // Thủy (ally) vs Hỏa (foe) at the same point → Thủy khắc Hỏa.
  Vector3 mid = { base.x + 2.0f, 0.5f, base.z };
  Combat_SubmitProjectile(ally, ELEM_WATER, mid, 0.3f, 10.0f, 0.0f, 101);
  Combat_SubmitProjectile(foe,  ELEM_FIRE,  mid, 0.3f, 10.0f, 0.0f, 202);
  Combat_Update(1.0f / 60.0f);
  ClashEvent ev[8];
  int n = Combat_PollEvents(ev, 8);
  ok = ok && AutoTest_ExpectTrue(n == 2, "clash produced 2 events", outReason, outReasonSize);
  bool waterWon = false, fireLost = false;
  for (int i = 0; i < n; i++) {
    if (ev[i].skillInstanceId == 101 && ev[i].outcome == CLASH_A_WINS && ev[i].otherElem == ELEM_FIRE) waterWon = true;
    if (ev[i].skillInstanceId == 202 && ev[i].outcome == CLASH_B_WINS && ev[i].otherElem == ELEM_WATER) fireLost = true;
  }
  ok = ok && AutoTest_ExpectTrue(waterWon, "water projectile won", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(fireLost, "fire projectile lost", outReason, outReasonSize);

  // Same team → pass through, no events.
  Combat_SubmitProjectile(ally, ELEM_WATER, mid, 0.3f, 10.0f, 0.0f, 103);
  Combat_SubmitProjectile(ally, ELEM_FIRE,  mid, 0.3f, 10.0f, 0.0f, 104);
  Combat_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Combat_PollEvents(ev, 8) == 0, "same-team pass through", outReason, outReasonSize);

  // Projectile → enemy agent: combat applies the damage + emits HIT_AGENT.
  Combat_SubmitProjectile(ally, ELEM_WATER, Entity_GetAgent(foe)->position, 0.3f, 10.0f, 0.0f, 105);
  Combat_Update(1.0f / 60.0f);
  n = Combat_PollEvents(ev, 8);
  ok = ok && AutoTest_ExpectTrue(n == 1 && ev[0].outcome == CLASH_HIT_AGENT && ev[0].otherAgentId == foe,
                                 "agent hit event", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(foe)->health, 90.0f, 0.01f,
                                      "combat applied projectile damage", outReason, outReasonSize);

  // Capacity: 128 colliders accepted, the 129th dropped.
  bool capOk = true;
  for (int i = 0; i < MAX_COMBAT_PROJECTILES; i++) {
    capOk = capOk && Combat_SubmitProjectile(ally, ELEM_WOOD,
              (Vector3){ base.x + 100.0f + i, 0, base.z }, 0.1f, 1.0f, 0.0f, 300 + i);
  }
  ok = ok && AutoTest_ExpectTrue(capOk, "128 submissions accepted", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!Combat_SubmitProjectile(ally, ELEM_WOOD, base, 0.1f, 1.0f, 0.0f, 999),
                                 "129th submission rejected", outReason, outReasonSize);
  Combat_Update(1.0f / 60.0f);
  Combat_PollEvents(ev, 8);

  Entity_ApplyDamage(ally, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(foe,  1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 4 DoD: intent-driven movement (with speedMult), dash burst,
// meditate start + cancel-on-move, mana-charged skill cast with cooldown.
// Drives Control_Apply directly — ReadIntent needs a real keyboard.
static AutoTestResult AutoTest_ControlStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Vector3 base = { 0.0f, 0.0f, -4.0f };
  int agent = Entity_SpawnAgent(base, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(agent >= 0, "spawned control agent", outReason, outReasonSize);
  Control_Init(agent);

  // Movement: 1s at 3.5 m/s.
  PlayerIntent intent = { 0 };
  intent.castSkillSlot = -1;
  intent.moveDir = (Vector2){ 1.0f, 0.0f };
  Control_Apply(&intent, 1.0f);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(agent)->position.x, base.x + 3.5f, 0.01f,
                                      "moved 3.5m in 1s", outReason, outReasonSize);

  // speedMult respected: 2x modifier → 7m in 1s.
  Entity_AddModifier(agent, 2.0f, 5.0f);
  float xBefore = Entity_GetAgent(agent)->position.x;
  Control_Apply(&intent, 1.0f);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(agent)->position.x - xBefore, 7.0f, 0.01f,
                                      "speedMult doubles move speed", outReason, outReasonSize);

  // Meditate starts when idle, breaks on a real move.
  PlayerIntent idle = { 0 };
  idle.castSkillSlot = -1;
  idle.meditate = true;
  Control_Apply(&idle, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Entity_IsMeditating(agent), "meditate via intent", outReason, outReasonSize);
  Control_Apply(&intent, 1.0f / 60.0f); // move again
  ok = ok && AutoTest_ExpectTrue(!Entity_IsMeditating(agent), "moving breaks meditate", outReason, outReasonSize);

  // Dash via intent: burst timer + cooldown armed.
  PlayerIntent dash = { 0 };
  dash.castSkillSlot = -1;
  dash.dash = true;
  dash.moveDir = (Vector2){ 1.0f, 0.0f };
  Control_Apply(&dash, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(agent)->dashTimer > 0.0f, "dash burst armed", outReason, outReasonSize);

  // Cast slot 0: equip a real registered skill, cast at a nearby aim point —
  // mana must drop (CastSkill charges it) and the cooldown must engage.
  ok = ok && AutoTest_ExpectTrue(GetRegisteredSkillCount() > 0, "have a skill to equip", outReason, outReasonSize);
  Entity_SetEquippedSkill(agent, 0, 0, Entity_GetAgent(agent)->currentElement);
  float manaBefore = Entity_GetAgent(agent)->mana;
  PlayerIntent cast = { 0 };
  cast.castSkillSlot = 0;
  cast.aimPoint = (Vector3){ Entity_GetAgent(agent)->position.x + 2.0f, 0.0f,
                             Entity_GetAgent(agent)->position.z };
  Control_Apply(&cast, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(agent)->mana < manaBefore,
                                 "cast charged mana", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!SkillManager_CanCast(0, agent),
                                 "cast engaged cooldown", outReason, outReasonSize);

  Entity_ApplyDamage(agent, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 6 DoD: balanced 2 Âm + 2 Dương loadout enters Thái Cực; taiji
// projectiles are immune to elemental counters; Phong deflects enemy
// projectiles + exposes its center for Lôi; Lôi damages through the
// team-aware AoE; mana hitting zero exits the state.
static AutoTestResult AutoTest_TaijiStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Vector3 base = { 12.0f, 0.0f, -2.0f };
  int a = Entity_SpawnAgent(base, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int b = Entity_SpawnAgent((Vector3){ base.x + 2.0f, 0, base.z }, 100.0f, 0, TEAM_ENEMY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(a >= 0 && b >= 0, "spawned taiji agents", outReason, outReasonSize);

  // 2 Âm (Thủy/Mộc) + 2 Dương (Hỏa/Kim) → Thái Cực.
  Entity_SetEquippedSkill(a, 0, 10, 0); // water (âm)
  Entity_SetEquippedSkill(a, 1, 11, 1); // wood  (âm)
  Entity_SetEquippedSkill(a, 2, 12, 2); // fire  (dương)
  Entity_SetEquippedSkill(a, 3, 13, 4); // metal (dương)
  ok = ok && AutoTest_ExpectTrue(Entity_IsTaijiActive(a), "2am+2duong enters taiji", outReason, outReasonSize);

  // Immunity: taiji FIRE beats non-taiji WATER (matrix says the opposite).
  Vector3 mid = { base.x + 1.0f, 0.5f, base.z };
  Combat_SubmitProjectile(a, ELEM_FIRE,  mid, 0.3f, 10.0f, 0.0f, 401);
  Combat_SubmitProjectile(b, ELEM_WATER, mid, 0.3f, 10.0f, 0.0f, 402);
  Combat_Update(1.0f / 60.0f);
  ClashEvent ev[8];
  int n = Combat_PollEvents(ev, 8);
  bool taijiWon = false;
  for (int i = 0; i < n; i++) {
    if (ev[i].skillInstanceId == 401 && ev[i].outcome == CLASH_A_WINS) taijiWon = true;
  }
  ok = ok && AutoTest_ExpectTrue(taijiWon, "taiji projectile immune to counter", outReason, outReasonSize);

  // Phong deflect: enemy projectile inside the radius dies with a B_WINS
  // event for its owner (deflect is called between submissions and update).
  Combat_SubmitProjectile(b, ELEM_WATER, mid, 0.3f, 10.0f, 0.0f, 403);
  int deflected = Combat_DeflectProjectilesInRadius(mid, 5.0f, a);
  Combat_Update(1.0f / 60.0f);
  n = Combat_PollEvents(ev, 8);
  ok = ok && AutoTest_ExpectTrue(deflected == 1, "phong deflected 1 projectile", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(n == 1 && ev[0].skillInstanceId == 403 && ev[0].outcome == CLASH_B_WINS,
                                 "deflect event reached owner", outReason, outReasonSize);

  // Phong cast (mana -30) exposes its center; Lôi (mana -45) strikes it and
  // damages the enemy standing inside through the team-aware AoE.
  int phongIdx = Skill_GetIndexByName("TAIJI_PHONG");
  int loiIdx   = Skill_GetIndexByName("TAIJI_LOI");
  ok = ok && AutoTest_ExpectTrue(phongIdx >= 0 && loiIdx >= 0, "taiji skills registered", outReason, outReasonSize);
  Vector3 vortex = Entity_GetAgent(b)->position;
  ok = ok && AutoTest_ExpectTrue(CastSkill(phongIdx, a, base, vortex, (SkillParams){ .level = 1 }),
                                 "phong cast went through", outReason, outReasonSize);
  Vector3 phongCenter;
  ok = ok && AutoTest_ExpectTrue(TaijiPhong_GetActiveCenter(&phongCenter), "phong center exposed", outReason, outReasonSize);
  float hpBefore = Entity_GetAgent(b)->health;
  ok = ok && AutoTest_ExpectTrue(CastSkill(loiIdx, a, base, base, (SkillParams){ .level = 1 }),
                                 "loi cast went through", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(b)->health < hpBefore, "loi damaged enemy at vortex", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(a)->mana, 25.0f, 0.1f, "phong+loi drained 75 mana", outReason, outReasonSize);

  // Dry pool exits the state.
  Entity_TrySpendMana(a, Entity_GetAgent(a)->mana);
  Entity_Update(1.0f / 600.0f);
  ok = ok && AutoTest_ExpectTrue(!Entity_IsTaijiActive(a), "empty mana exits taiji", outReason, outReasonSize);

  Entity_ApplyDamage(a, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(b, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 7 DoD: full match loop — intro title card spawns the boss and
// enters FIGHTING; boss death → VICTORY; player death → DEFEAT; reset
// re-arms the intro. Zone rule table sanity-checked directly.
static AutoTestResult AutoTest_GameModeStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Camera3D cam = { .position = { 10, 8, 10 }, .target = { 6, 0, 4.4f },
                   .up = { 0, 1, 0 }, .fovy = 45.0f, .projection = CAMERA_PERSPECTIVE };

  GameScreen_Init(s_player);
  bool ok = AutoTest_ExpectTrue(GameScreen_GetState() == GAME_ARENA_INTRO, "match starts in intro", outReason, outReasonSize);

  // 2s intro at fixed dt → boss spawns, fight begins.
  for (int i = 0; i < 130 && GameScreen_GetState() == GAME_ARENA_INTRO; i++) {
    GameScreen_Update(s_player, &cam, 1.0f / 60.0f);
  }
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_FIGHTING, "intro leads to fighting", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Boss_IsAlive(), "boss spawned by intro", outReason, outReasonSize);

  // Boss death → victory.
  Entity_ApplyDamage(Boss_GetAgentId(), 1e9f, (Vector3){ 0 });
  Boss_Update(0.0f);
  GameScreen_Update(s_player, &cam, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_VICTORY, "boss death gives victory", outReason, outReasonSize);

  // Fresh match, player death → defeat.
  GameScreen_Init(s_player);
  for (int i = 0; i < 130 && GameScreen_GetState() == GAME_ARENA_INTRO; i++) {
    GameScreen_Update(s_player, &cam, 1.0f / 60.0f);
  }
  Entity_ApplyDamage(s_player->agentId, 1e9f, (Vector3){ 0 });
  GameScreen_Update(s_player, &cam, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_DEFEAT, "player death gives defeat", outReason, outReasonSize);

  // Zone rule table (the one place gameplay rules live).
  ok = ok && AutoTest_ExpectFloatNear(GameRules_CooldownMult(0, NAT_RIVER), 0.5f, 0.001f, "thuy in river halves cooldown", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(GameRules_DamageMult(2, NAT_RIVER), 0.5f, 0.001f, "hoa in river halves damage", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(GameRules_GrantsStealth(1, NAT_FOREST), "moc in forest stealths", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(GameRules_CooldownMult(2, NAT_RIVER), 1.0f, 0.001f, "no rule leaves 1.0", outReason, outReasonSize);

  // Reset for a clean pool (respawns the player agent, clears the boss).
  GameScreen_Init(s_player);
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static void AutoTest_ResetArenaToDefault(void); // defined below (uses s_player)

// Step 0 (post-Module-7 hardening): real skills feed the combat registry.
// Casts a real FIRE dragon at an enemy agent and waits for the combat-
// applied hit (skills no longer damage projectiles themselves), and checks
// the match loop equips the default loadout.
static int s_step0Ally = -1, s_step0Foe = -1;
static AutoTestResult AutoTest_SkillRegistryStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    GameScreen_Init(s_player); // also applies the default loadout
    AutoTest_ResetArenaToDefault();
    const Agent *pa = Entity_GetAgent(s_player->agentId);
    if (!AutoTest_ExpectTrue(pa && pa->equippedSkills[0] >= 0 && pa->equippedSkills[3] >= 0,
                             "default loadout equipped", outReason, outReasonSize)) return AUTOTEST_FAIL;

    s_step0Ally = Entity_SpawnAgent((Vector3){ 0, 0, 0 }, 100.0f, 2, TEAM_ALLY, ARCH_HERO);
    s_step0Foe  = Entity_SpawnAgent((Vector3){ 4.0f, 0, 0 }, 100.0f, 0, TEAM_ENEMY, ARCH_HERO);
    if (!AutoTest_ExpectTrue(s_step0Ally >= 0 && s_step0Foe >= 0, "spawned duel agents", outReason, outReasonSize)) return AUTOTEST_FAIL;

    int fireIdx = Skill_GetIndexByName("FIRE");
    if (!AutoTest_ExpectTrue(fireIdx >= 0, "FIRE registered", outReason, outReasonSize)) return AUTOTEST_FAIL;
    Vector3 from = { 0, 0.5f, 0 };
    if (!AutoTest_ExpectTrue(CastSkill(fireIdx, s_step0Ally, from, (Vector3){ 4.0f, 0.5f, 0 },
                                       (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f }),
                             "fire cast went through", outReason, outReasonSize)) return AUTOTEST_FAIL;
    return AUTOTEST_RUNNING;
  }

  const Agent *foe = Entity_GetAgent(s_step0Foe);
  if (foe == NULL || foe->health < 100.0f) {
    // Combat registry applied the dragon's hit (or knocked the foe out).
    Entity_ApplyDamage(s_step0Ally, 1e9f, (Vector3){ 0 });
    if (foe) Entity_ApplyDamage(s_step0Foe, 1e9f, (Vector3){ 0 });
    GameScreen_Init(s_player);
    return AUTOTEST_PASS;
  }
  return AUTOTEST_RUNNING; // maxFrames timeout fails the case
}

// The match (game_mode_loop test) pins VERDANT_PATH + its ring-out bounds
// and parks the player at the island spawn. Tests that spawn actors at
// DEFAULT_ARENA coordinates must restore that world first, or their agents
// (and the parked player) fall out of the new bounds mid-test.
static void AutoTest_ResetArenaToDefault(void) {
  for (int i = 0; i < MapManager_GetCount(); i++) {
    if (strcmp(MapManager_GetName(i), "DEFAULT_ARENA") == 0) {
      MapManager_SetActiveIndex(i);
      break;
    }
  }
  Entity_SetArenaBounds((Vector3){ 6.0f, 0.0f, 4.4f }, 18.0f);
  s_player->position = (Vector3){ -11.0f, 0.0f, 4.4f }; // sandbox home
  Entity_SetPosition(s_player->agentId, s_player->position);
}

// Module 11 (wire core) DoD: PlayerIntent survives a pack/unpack round trip
// bit-exact; an agent-pool snapshot packs and parses back with matching
// fields; corrupted/mismatched packets are rejected.
static AutoTestResult AutoTest_NetWireStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault(); // snapshot probe spawns at arena coords

  // Intent round trip.
  PlayerIntent in = { 0 };
  in.moveDir = (Vector2){ 0.7071f, -0.7071f };
  in.jump = true; in.meditate = true;
  in.castSkillSlot = 2;
  in.basicAttack = 3; // KICK + 1 — v2 melee field must round-trip
  in.aimPoint = (Vector3){ 6.25f, 0.0f, -3.5f };
  unsigned char buf[64];
  int len = Net_PackIntent(&in, buf, sizeof(buf));
  bool ok = AutoTest_ExpectTrue(len > 0, "intent packed", outReason, outReasonSize);
  PlayerIntent out = { 0 };
  ok = ok && AutoTest_ExpectTrue(Net_UnpackIntent(&out, buf, len), "intent unpacked", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(out.jump && !out.dash && out.meditate && out.castSkillSlot == 2 &&
                                 out.basicAttack == 3,
                                 "intent flags/slot survive", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(out.moveDir.x, in.moveDir.x, 0.0001f, "moveDir survives", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(out.aimPoint.z, in.aimPoint.z, 0.0001f, "aimPoint survives", outReason, outReasonSize);

  // Corruption / version guard.
  buf[2] = (unsigned char)(NET_PROTOCOL_VERSION + 1);
  ok = ok && AutoTest_ExpectTrue(!Net_UnpackIntent(&out, buf, len), "version mismatch rejected", outReason, outReasonSize);

  // Snapshot round trip: a known agent shows up with matching fields.
  int probe = Entity_SpawnAgent((Vector3){ -4.0f, 0.0f, 8.0f }, 77.0f, 4, TEAM_ENEMY, ARCH_MINION);
  ok = ok && AutoTest_ExpectTrue(probe >= 0, "spawned snapshot probe", outReason, outReasonSize);
  static unsigned char snap[16 * 1024];
  int snapLen = Net_PackAgentSnapshot(snap, (int)sizeof(snap));
  ok = ok && AutoTest_ExpectTrue(snapLen > 0, "snapshot packed", outReason, outReasonSize);
  static NetAgentState states[MAX_AGENTS];
  int n = Net_UnpackAgentSnapshot(states, MAX_AGENTS, snap, snapLen);
  ok = ok && AutoTest_ExpectTrue(n > 0, "snapshot unpacked", outReason, outReasonSize);
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (states[i].agentId != (unsigned char)probe) continue;
    found = states[i].team == (unsigned char)TEAM_ENEMY &&
            states[i].archetype == (unsigned char)ARCH_MINION &&
            states[i].element == 4 &&
            fabsf(states[i].health - 77.0f) < 0.001f &&
            fabsf(states[i].position.z - 8.0f) < 0.001f;
  }
  ok = ok && AutoTest_ExpectTrue(found, "probe agent fields survive snapshot", outReason, outReasonSize);

  // Roster round trip (protocol v3 — multi-peer room list).
  NetRosterEntry roster[3] = {
    { 0, 0, 12, NET_ROSTER_OCCUPIED | NET_ROSTER_HOST },
    { 1, 1, 34, NET_ROSTER_OCCUPIED },
    { 5, 0, NET_ROSTER_NONE, NET_ROSTER_OCCUPIED | NET_ROSTER_BOT },
  };
  unsigned char rbuf[64];
  int rlen = Net_PackRoster(roster, 3, rbuf, (int)sizeof(rbuf));
  ok = ok && AutoTest_ExpectTrue(rlen > 0, "roster packed", outReason, outReasonSize);
  NetRosterEntry rout[NET_MAX_PLAYERS];
  int rn = Net_UnpackRoster(rout, NET_MAX_PLAYERS, rbuf, rlen);
  ok = ok && AutoTest_ExpectTrue(rn == 3, "roster unpacked 3 entries", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(rn == 3 &&
                                 rout[0].flags == (NET_ROSTER_OCCUPIED | NET_ROSTER_HOST) &&
                                 rout[1].slot == 1 && rout[1].team == 1 && rout[1].agentId == 34 &&
                                 rout[2].agentId == NET_ROSTER_NONE &&
                                 (rout[2].flags & NET_ROSTER_BOT) != 0,
                                 "roster fields survive", outReason, outReasonSize);
  rbuf[2] = (unsigned char)(NET_PROTOCOL_VERSION + 1);
  ok = ok && AutoTest_ExpectTrue(Net_UnpackRoster(rout, NET_MAX_PLAYERS, rbuf, rlen) < 0,
                                 "roster version mismatch rejected", outReason, outReasonSize);

  // Room management (Đợt A2): a real host endpoint, no peers needed. The
  // roster starts as host-only; bots fill sides; toggling flips teams.
  ok = ok && AutoTest_ExpectTrue(Net_StartHost(7997), "host endpoint up", outReason, outReasonSize);
  int rc = Net_GetRoster(rout, NET_MAX_PLAYERS);
  ok = ok && AutoTest_ExpectTrue(rc == 1 && (rout[0].flags & NET_ROSTER_HOST) &&
                                 rout[0].team == 0,
                                 "fresh room = host only, team 0", outReason, outReasonSize);
  Net_HostAddBot(1);
  Net_HostAddBot(1);
  rc = Net_GetRoster(rout, NET_MAX_PLAYERS);
  ok = ok && AutoTest_ExpectTrue(rc == 3 && (rout[1].flags & NET_ROSTER_BOT) &&
                                 rout[1].team == 1 && rout[2].team == 1,
                                 "two bots on side 1", outReason, outReasonSize);
  Net_HostToggleTeam(1); // bot #1 → side 0
  rc = Net_GetRoster(rout, NET_MAX_PLAYERS);
  int bots0 = 0, bots1 = 0;
  for (int i = 0; i < rc; i++)
    if (rout[i].flags & NET_ROSTER_BOT) { if (rout[i].team == 0) bots0++; else bots1++; }
  ok = ok && AutoTest_ExpectTrue(bots0 == 1 && bots1 == 1, "bot moved sides", outReason, outReasonSize);
  Net_HostToggleTeam(0); // host flips to side 1
  rc = Net_GetRoster(rout, NET_MAX_PLAYERS);
  ok = ok && AutoTest_ExpectTrue(rc == 3 && rout[0].team == 1, "host flipped team", outReason, outReasonSize);
  Net_HostRemoveBot(0);
  Net_HostRemoveBot(1);
  rc = Net_GetRoster(rout, NET_MAX_PLAYERS);
  ok = ok && AutoTest_ExpectTrue(rc == 1, "bots removed", outReason, outReasonSize);
  Net_HostStartMatch();
  ok = ok && AutoTest_ExpectTrue(Net_ConsumeMatchStart(), "start flag arms", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!Net_ConsumeMatchStart(), "start flag consumed once", outReason, outReasonSize);
  Net_Stop();
  ok = ok && AutoTest_ExpectTrue(Net_GetMode() == NET_MODE_OFF, "net stopped clean", outReason, outReasonSize);

  Entity_ApplyDamage(probe, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Đợt A3 DoD: team-battle elimination — the exact count game_screen's
// FIGHTING check reads must hit zero when one side is wiped, and only then.
static AutoTestResult AutoTest_TeamEliminationStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault();

  // Clean slate: retire every leftover ENEMY hero from earlier cases so the
  // baseline is unambiguous (ALLY keeps the sandbox player — counted).
  for (int i = 0; i < MAX_AGENTS; i++) {
    const Agent *a = Entity_GetAgent(i);
    if (a != NULL && a->archetype == ARCH_HERO && a->team == TEAM_ENEMY)
      Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 });
  }
  bool ok = AutoTest_ExpectTrue(GameRules_CountAliveHeroes(TEAM_ENEMY) == 0,
                                "enemy side starts empty", outReason, outReasonSize);

  int allyBase = GameRules_CountAliveHeroes(TEAM_ALLY);
  int a1 = Entity_SpawnAgent((Vector3){ -2.0f, 0, 0 }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int a2 = Entity_SpawnAgent((Vector3){ -2.0f, 0, 2.0f }, 100.0f, 1, TEAM_ALLY, ARCH_HERO);
  int e1 = Entity_SpawnAgent((Vector3){  2.0f, 0, 0 }, 100.0f, 2, TEAM_ENEMY, ARCH_HERO);
  int e2 = Entity_SpawnAgent((Vector3){  2.0f, 0, 2.0f }, 100.0f, 3, TEAM_ENEMY, ARCH_HERO);
  ok = ok && AutoTest_ExpectTrue(a1 >= 0 && a2 >= 0 && e1 >= 0 && e2 >= 0,
                                 "spawned 2v2", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(GameRules_CountAliveHeroes(TEAM_ALLY) == allyBase + 2 &&
                                 GameRules_CountAliveHeroes(TEAM_ENEMY) == 2,
                                 "2v2 counted", outReason, outReasonSize);

  Entity_ApplyDamage(e1, 1e9f, (Vector3){ 0 });
  ok = ok && AutoTest_ExpectTrue(GameRules_CountAliveHeroes(TEAM_ENEMY) == 1,
                                 "one enemy down, side still alive", outReason, outReasonSize);
  Entity_ApplyDamage(e2, 1e9f, (Vector3){ 0 });
  ok = ok && AutoTest_ExpectTrue(GameRules_CountAliveHeroes(TEAM_ENEMY) == 0 &&
                                 GameRules_CountAliveHeroes(TEAM_ALLY) == allyBase + 2,
                                 "enemy side wiped -> elimination fires for ALLY",
                                 outReason, outReasonSize);

  Entity_ApplyDamage(a1, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(a2, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Đợt A4 DoD: hero-bot brain (spawns, fights, stays on the arena) + the
// handicap table + HP scaling for the short-handed side.
static AutoTestResult AutoTest_HeroBotHandicapStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault();
  AI_ClearHeroBots();

  // Handicap table: identity at 0, per-player steps, capped at 3.
  TeamHandicap h0 = GameRules_HandicapFor(0);
  TeamHandicap h2 = GameRules_HandicapFor(2);
  TeamHandicap h9 = GameRules_HandicapFor(9);
  bool ok = AutoTest_ExpectFloatNear(h0.maxHpMult, 1.0f, 0.001f, "deficit 0 = no buff", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(h2.maxHpMult, 1.30f, 0.001f, "deficit 2 HP mult", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(h9.maxHpMult, 1.45f, 0.001f, "deficit caps at 3", outReason, outReasonSize);

  // HP scaling applies to max + current.
  int probe = Entity_SpawnAgent((Vector3){ 0, 0, 0 }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  Entity_ScaleMaxHealth(probe, h2.maxHpMult);
  const Agent *pa = Entity_GetAgent(probe);
  ok = ok && AutoTest_ExpectTrue(pa != NULL && fabsf(pa->maxHealth - 130.0f) < 0.01f &&
                                 fabsf(pa->health - 130.0f) < 0.01f,
                                 "HP scaled 100 -> 130", outReason, outReasonSize);

  // Bot brain: spawn one ENEMY bot vs an ALLY target, tick 5 simulated
  // seconds — it must survive, stay inside the ring, and have cast at
  // least once (casting spends mana).
  int bot = AI_SpawnHeroBot((Vector3){ 3.0f, 0, 4.4f }, TEAM_ENEMY);
  ok = ok && AutoTest_ExpectTrue(bot >= 0, "bot spawned", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(AI_GetHeroBotCount(TEAM_ENEMY) == 1, "bot counted", outReason, outReasonSize);
  {
    static const struct { const char *name; int element; } kBotLoad[2] = {
      { "GLACIAL_CANNON", 0 }, { "FIRE", 2 },
    };
    for (int slot = 0; slot < 2; slot++) {
      int idx = Skill_GetIndexByName(kBotLoad[slot].name);
      if (idx >= 0) Entity_SetEquippedSkill(bot, slot, idx, kBotLoad[slot].element);
    }
  }
  /* Drain anything an earlier case logged, so the poll after the loop counts
   * only this bot's casts. */
  {
    HeroBotCast drain[16];
    AI_PollHeroCasts(drain, 16);
  }
  for (int i = 0; i < 300; i++) { // ~5s @60fps
    AI_Update(1.0f / 60.0f);
    Entity_Update(1.0f / 60.0f);
    UpdateSkillManager(1.0f / 60.0f, (Vector3){ 0, 0, 0 }, 0.35f);
  }
  const Agent *ba = Entity_GetAgent(bot);
  ok = ok && AutoTest_ExpectTrue(ba != NULL, "bot alive after 5s", outReason, outReasonSize);
  if (ba != NULL) {
    float ex = ba->position.x - 6.0f, ez = ba->position.z - 4.4f;
    ok = ok && AutoTest_ExpectTrue(sqrtf(ex * ex + ez * ez) < 18.0f,
                                   "bot stayed on the arena", outReason, outReasonSize);
    /* Ask the cast LOG, not the mana level. "mana is below max right now" is a
     * state standing in for an event, and the two come apart: mana regenerates
     * at 5/s (entities.c MANA_REGEN_PER_SEC), so over these 5 simulated seconds
     * a bot that casts early is back at full before the check runs and the
     * assertion fails on a bot that did exactly what was asked. Measured 4
     * failures in 6 runs that way; the RNG behind the bot's think cadence only
     * decides WHEN it casts, which is what made it look random. */
    HeroBotCast casts[16];
    int castCount = AI_PollHeroCasts(casts, 16);
    ok = ok && AutoTest_ExpectTrue(castCount > 0,
                                   "bot cast at least once", outReason, outReasonSize);
  }

  // Đợt A5: free-cast mode bypasses the mana gate (client VFX replay) —
  // and turning it off restores the gate.
  {
    int caster = Entity_SpawnAgent((Vector3){ 1.0f, 0, 1.0f }, 100.0f, 2, TEAM_ALLY, ARCH_HERO);
    int fire = Skill_GetIndexByName("FIRE");
    const Agent *ca = Entity_GetAgent(caster);
    if (ca != NULL) Entity_TrySpendMana(caster, ca->mana); // drain exactly
    SkillParams sp = { .level = 1, .quantity = 1, .sizeScale = 1.0f };
    ok = ok && AutoTest_ExpectTrue(fire >= 0 &&
                                   !CastSkill(fire, caster, (Vector3){ 1, 0, 1 }, (Vector3){ 5, 0, 5 }, sp),
                                   "drained caster rejected", outReason, outReasonSize);
    SkillManager_SetFreeCast(true);
    ok = ok && AutoTest_ExpectTrue(CastSkill(fire, caster, (Vector3){ 1, 0, 1 }, (Vector3){ 5, 0, 5 }, sp),
                                   "free-cast bypasses mana gate", outReason, outReasonSize);
    SkillManager_SetFreeCast(false);
    Entity_ApplyDamage(caster, 1e9f, (Vector3){ 0 });
  }

  AI_ClearHeroBots();
  Entity_ApplyDamage(probe, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 10 DoD: mana-gated deploy, river resonance deepens Hàn Băng's slow
// (0.4 vs 0.6 speedMult), duration expiry frees the slot, pool caps at 4.
static AutoTestResult AutoTest_FormationStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault(); // formation resonance reads DEFAULT_ARENA's river

  Vector3 river = { 0.0f, 0.0f, -2.0f }; // DEFAULT_ARENA river zone center
  int owner = Entity_SpawnAgent((Vector3){ river.x + 1.0f, 0, river.z }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int foe   = Entity_SpawnAgent(river, 100.0f, 2, TEAM_ENEMY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(owner >= 0 && foe >= 0, "spawned formation actors", outReason, outReasonSize);

  // Mana gate: drain, deploy must fail, nothing sticks.
  Entity_TrySpendMana(owner, 95.0f);
  ok = ok && AutoTest_ExpectTrue(Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner) < 0,
                                 "deploy rejected on low mana", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Formation_GetActiveCount() == 0, "no ghost formation", outReason, outReasonSize);

  // Fresh owner: deploy on the river → resonant Hàn Băng (slow 0.4).
  int owner2 = Entity_SpawnAgent((Vector3){ river.x - 1.0f, 0, river.z }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int slot = Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner2);
  ok = ok && AutoTest_ExpectTrue(slot >= 0, "deploy went through", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(owner2)->mana, 70.0f, 0.1f,
                                      "deploy charged 30 mana", outReason, outReasonSize);
  for (int i = 0; i < 3; i++) Formation_Update(0.4f); // past the 0.5s refresh tick
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetSpeedMult(foe), 0.4f, 0.01f,
                                      "resonant slow (0.4) on enemy in circle", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetSpeedMult(owner2), 1.0f, 0.01f,
                                      "own team not slowed", outReason, outReasonSize);

  // Duration expiry frees the slot.
  for (int i = 0; i < 30; i++) Formation_Update(0.5f); // 15s >> 10s duration
  ok = ok && AutoTest_ExpectTrue(Formation_GetActiveCount() == 0, "formation expired", outReason, outReasonSize);

  // Pool cap: 4 deploys fit (2 owners × 2 × 30 mana), the 5th is rejected.
  int owner3 = Entity_SpawnAgent((Vector3){ river.x, 0, river.z + 1.0f }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  bool four = true;
  four = four && Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner2) >= 0;
  four = four && Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner2) >= 0;
  four = four && Formation_Deploy(&FORMATION_CUU_THIEN_LOI_DONG, river, owner3) >= 0;
  four = four && Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner3) >= 0;
  ok = ok && AutoTest_ExpectTrue(four, "4 deploys fill the pool", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Formation_Deploy(&FORMATION_HAN_BANG_THUY_TUYET, river, owner3) < 0,
                                 "5th deploy rejected (pool full)", outReason, outReasonSize);
  for (int i = 0; i < 30; i++) Formation_Update(0.5f); // let them all expire

  Entity_ApplyDamage(owner, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(owner2, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(owner3, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(foe, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 9 DoD: auto-target prefers an incoming enemy projectile (đối-đòn)
// over the boss, falls back to the boss, and reports no target when neither
// exists.
static AutoTestResult AutoTest_AutoTargetStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault();

  Vector3 base = { 6.0f, 0.0f, -4.0f };
  int ally = Entity_SpawnAgent(base, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  int foe  = Entity_SpawnAgent((Vector3){ base.x, 0, base.z - 3.0f }, 100.0f, 2, TEAM_ENEMY, ARCH_HERO);
  int bossId = Boss_Spawn(&BOSS_HAC_DIEN_TON_GIA, (Vector3){ base.x, 0, base.z - 6.0f }, TEAM_ENEMY);
  bool ok = AutoTest_ExpectTrue(ally >= 0 && foe >= 0 && bossId >= 0, "spawned aim actors", outReason, outReasonSize);

  // Priority 2 first: no projectiles in flight → aim at the boss core.
  Combat_Update(1.0f / 60.0f); // empty resolve — clears the projectile snapshot
  bool has = false;
  Vector3 aim = UI_GetAutoAimPoint(ally, &has);
  ok = ok && AutoTest_ExpectTrue(has, "boss targeted when no projectile", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(aim.z, base.z - 6.0f, 0.01f, "aim at boss position", outReason, outReasonSize);

  // Priority 1: an enemy projectile in flight (5m away, boss is 6m) wins.
  Combat_SubmitProjectile(foe, ELEM_FIRE, (Vector3){ base.x + 5.0f, 0.5f, base.z }, 0.3f, 10.0f, 0.0f, 901);
  Combat_Update(1.0f / 60.0f); // snapshot now holds the projectile
  aim = UI_GetAutoAimPoint(ally, &has);
  ok = ok && AutoTest_ExpectTrue(has, "projectile targeted", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(aim.x, base.x + 5.0f, 0.01f, "aim at projectile over boss", outReason, outReasonSize);

  // No boss, no projectile → no target.
  Entity_ApplyDamage(bossId, 1e9f, (Vector3){ 0 });
  Boss_Update(0.0f);
  Combat_Update(1.0f / 60.0f); // empty resolve clears the snapshot again
  UI_GetAutoAimPoint(ally, &has);
  ok = ok && AutoTest_ExpectTrue(!has, "no target when field is clear", outReason, outReasonSize);

  Entity_ApplyDamage(ally, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(foe, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 8 DoD: minion waves spawn around a boss inheriting team/element,
// march toward the opposing boss, self-destruct with team-aware AoE + a
// poll event, and the pool absorbs a 40-minion wave.
static AutoTestResult AutoTest_MinionAIStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;
  AutoTest_ResetArenaToDefault();

  // Two "boss" pole agents inside the arena, 4m apart.
  Vector3 allyPole = { -2.0f, 0.0f, -6.0f };
  Vector3 foePole  = { -6.0f, 0.0f, -6.0f };
  int allyBoss = Entity_SpawnAgent(allyPole, 200.0f, 1, TEAM_ALLY, ARCH_BOSS);
  int foeBoss  = Entity_SpawnAgent(foePole, 200.0f, 2, TEAM_ENEMY, ARCH_BOSS);
  bool ok = AutoTest_ExpectTrue(allyBoss >= 0 && foeBoss >= 0, "spawned pole bosses", outReason, outReasonSize);

  // Wave inherits the ally boss's team + element.
  int spawned = AI_SpawnMinionWave(allyBoss, 4);
  ok = ok && AutoTest_ExpectTrue(spawned == 4, "wave of 4 spawned", outReason, outReasonSize);
  int ids[16];
  int minionCount = 0;
  int n = Entity_GetNearbyTargetsTeam(allyPole, 3.0f, TEAM_ALLY, ids, 16);
  for (int i = 0; i < n; i++) {
    const Agent *a = Entity_GetAgent(ids[i]);
    if (a && a->archetype == ARCH_MINION) {
      minionCount++;
      ok = ok && AutoTest_ExpectTrue(a->currentElement == 1, "minion inherits element", outReason, outReasonSize);
    }
  }
  ok = ok && AutoTest_ExpectTrue(minionCount == 4, "minions near their boss", outReason, outReasonSize);

  // March + detonation: drive AI_Update; minions cover ~4m at 2 m/s and
  // blow up on the enemy boss. Collect explosion events as we go (the main
  // loop's own poll runs earlier in the frame, so these are ours).
  float foeHpBefore = Entity_GetAgent(foeBoss)->health;
  int booms = 0;
  MinionExplosion ev[8];
  for (int step = 0; step < 60; step++) {
    AI_Update(0.1f);
    booms += AI_PollExplosions(ev, 8);
  }
  ok = ok && AutoTest_ExpectTrue(booms >= 1, "explosion events emitted", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(foeBoss)->health < foeHpBefore,
                                 "explosions damaged enemy boss", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(allyBoss)->health, 200.0f, 0.01f,
                                      "friendly boss untouched", outReason, outReasonSize);

  // Capacity: a 40-minion wave fits the shared pool.
  ok = ok && AutoTest_ExpectTrue(AI_SpawnMinionWave(allyBoss, 40) == 40, "40-minion wave", outReason, outReasonSize);

  // Cleanup: kill every remaining test agent (minions included).
  for (int i = 0; i < MAX_AGENTS; i++) {
    const Agent *a = Entity_GetAgent(i);
    if (!a) continue;
    if (a->archetype == ARCH_MINION || i == allyBoss || i == foeBoss) {
      Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 });
    }
  }
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Module 5 DoD: boss spawns as ARCH_BOSS pool agent, phase follows %HP with
// biến hệ (element change = visual cue source), AI cast fires through the
// skill manager at a nearby opposing-team target, death via the normal
// entities damage path ends the fight.
static AutoTestResult AutoTest_BossStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Vector3 pos = { -6.0f, 0.0f, -4.0f }; // inside arena, clear of other cases
  int bossId = Boss_Spawn(&BOSS_HAC_DIEN_TON_GIA, pos, TEAM_ENEMY);
  bool ok = AutoTest_ExpectTrue(bossId >= 0, "boss spawned", outReason, outReasonSize);
  const Agent *boss = Entity_GetAgent(bossId);
  ok = ok && AutoTest_ExpectTrue(boss && boss->archetype == ARCH_BOSS, "boss archetype", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(boss && boss->currentElement == 0, "phase 0 element = water", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Boss_GetPhase() == 0, "starts in phase 0", outReason, outReasonSize);

  // A player-side target in range so the AI has someone to shoot.
  int hero = Entity_SpawnAgent((Vector3){ pos.x + 5.0f, 0, pos.z }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  ok = ok && AutoTest_ExpectTrue(hero >= 0, "spawned boss target", outReason, outReasonSize);

  // Phase transitions follow %HP (400 max: 75% at 300, 50% at 200).
  Entity_ApplyDamage(bossId, 150.0f, (Vector3){ 0 }); // 62.5%
  Boss_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Boss_GetPhase() == 1, "phase 1 at 62.5% HP", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(bossId)->currentElement == 2, "biến hệ to fire", outReason, outReasonSize);

  Entity_ApplyDamage(bossId, 100.0f, (Vector3){ 0 }); // 37.5%
  Boss_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(Boss_GetPhase() == 2, "phase 2 at 37.5% HP", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Entity_GetAgent(bossId)->currentElement == 3, "biến hệ to earth", outReason, outReasonSize);

  // AI cast: STONE_PRISON (phase 2 skill) cooldown must be engaged after an
  // update tick with a target in range — i.e. the boss actually cast.
  int phaseSkill = Skill_GetIndexByName("STONE_PRISON");
  ok = ok && AutoTest_ExpectTrue(phaseSkill >= 0, "phase skill registered", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!SkillManager_CanCast(phaseSkill, bossId), "boss cast engaged cooldown", outReason, outReasonSize);

  // Death through the normal entities path ends the fight.
  Entity_ApplyDamage(bossId, 1e9f, (Vector3){ 0 });
  Boss_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(!Boss_IsAlive() && Boss_GetPhase() == -1, "boss death ends fight", outReason, outReasonSize);

  Entity_ApplyDamage(hero, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}
// M3 guard — the INSTANCED shader permutation, on a real graphics context.
//
// This is the one thing core/tests/shader_permutation_test.c cannot answer: it
// checks the seam textually, but whether the variant COMPILES and LINKS needs a
// device. No VFX-test fixture reaches these programs (sandbox's harness takes
// one primary API per .inl, and water/ice_crystal.inl's is VFX_ComposeIceCrystal
// rather than the instanced VFX_DrawIceCrystalBurst beside it), so without this
// case the instanced path is first exercised by a player casting Glacial Cannon.
//
// The decisive check is the attribute, not the link. `in mat4 instanceTransform`
// must exist in the instanced program and must NOT exist in the other one —
// reading it unbound on a plain DrawMesh is undefined behaviour across drivers,
// and that is the entire reason this is a compile-time define rather than a
// runtime branch. rlvk resolves the name through its canonical attribute table
// (RLVK_ATTRIB_INSTANCE_TX), so a -1 here really does mean "not declared".
static AutoTestResult AutoTest_ShaderPermutationStep(int frameInCase, char *outReason, int outReasonSize) {
  (void)frameInCase;

  EffectMaterial            effect;   Material_LoadCustom(&effect, NULL);
  EffectMaterialInstanced   effectI;  EffectMaterialInstanced_Load(&effectI, NULL);
  CrystalMaterial           crystal;  CrystalMaterial_Load(&crystal, NULL);
  CrystalMaterialInstanced  crystalI; CrystalMaterialInstanced_Load(&crystalI, NULL);

  bool ok = true;

  ok &= AutoTest_ExpectTrue(effectI.shader.id != 0 && effectI.shader.locs != NULL,
                            "instanced effect_material program compiled and linked",
                            outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(crystalI.shader.id != 0 && crystalI.shader.locs != NULL,
                            "instanced crystal program compiled and linked",
                            outReason, outReasonSize);

  // Same .vs/.fs pair, different defines => different programs. If the cache
  // keyed on paths alone it would hand back the non-instanced program here, and
  // every instanced draw would read a matModel that is not per-instance.
  ok &= AutoTest_ExpectTrue(effectI.shader.id != effect.shader.id,
                            "shader cache keys on the defines (effect_material)",
                            outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(crystalI.shader.id != crystal.shader.id,
                            "shader cache keys on the defines (crystal)",
                            outReason, outReasonSize);

  ok &= AutoTest_ExpectTrue(GetShaderLocationAttrib(effectI.shader, "instanceTransform") >= 0,
                            "instanced effect variant declares instanceTransform",
                            outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(GetShaderLocationAttrib(effect.shader, "instanceTransform") < 0,
                            "non-instanced effect variant does NOT declare it",
                            outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(GetShaderLocationAttrib(crystalI.shader, "instanceTransform") >= 0,
                            "instanced crystal variant declares instanceTransform",
                            outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(GetShaderLocationAttrib(crystal.shader, "instanceTransform") < 0,
                            "non-instanced crystal variant does NOT declare it",
                            outReason, outReasonSize);

  // And the parameter table resolved against the real program: a table that
  // silently matched nothing would leave every material at shader defaults,
  // which renders as something rather than as an error.
  int resolved = 0;
  for (int i = 0; i < crystalI.layoutCount; i++)
    if (crystalI.locs[i] >= 0) resolved++;
  ok &= AutoTest_ExpectTrue(crystalI.layoutCount > 0 && resolved >= 8,
                            "the crystal parameter table resolved against the instanced program",
                            outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

void AutoTestCases_Register(PlayerEntity *player)
{
  s_player = player;
  AutoTest_Register("smoke_skill_manager_init", AutoTest_SmokeStep, 5);
  AutoTest_Register("entities_combat_v2", AutoTest_EntitiesCombatV2Step, 5);
  AutoTest_Register("map_trigger_zones", AutoTest_MapZonesStep, 5);
  AutoTest_Register("combat_clash_matrix", AutoTest_CombatClashStep, 5);
  AutoTest_Register("control_intent", AutoTest_ControlStep, 5);
  AutoTest_Register("boss_hac_dien", AutoTest_BossStep, 5);
  AutoTest_Register("taiji_state", AutoTest_TaijiStep, 5);
  AutoTest_Register("game_mode_loop", AutoTest_GameModeStep, 5);
  AutoTest_Register("skill_combat_registry", AutoTest_SkillRegistryStep, 300);
  AutoTest_Register("minion_ai", AutoTest_MinionAIStep, 5);
  AutoTest_Register("ui_auto_target", AutoTest_AutoTargetStep, 5);
  AutoTest_Register("formation_tran_phap", AutoTest_FormationStep, 5);
  AutoTest_Register("net_wire_format", AutoTest_NetWireStep, 5);
  AutoTest_Register("team_elimination", AutoTest_TeamEliminationStep, 5);
  AutoTest_Register("hero_bot_handicap", AutoTest_HeroBotHandicapStep, 5);
  AutoTest_Register("shader_permutation", AutoTest_ShaderPermutationStep, 5);
}
