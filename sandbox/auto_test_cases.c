#include "sandbox/auto_test_cases.h"

#include "sandbox/auto_test.h"
#include "skills/skill_manager.h"
#include "core/material/material_system.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "game/game_screen.h"
#include "game/game_rules.h"
#include "game/quest_datau.h"
#include "open_world/ow_world.h"
#include "open_world/ow_weather.h"
#include "open_world/ow_sandbox_modes.h"
#include "open_world/realms/ow_desert_earth.h"
#include "net/net_prediction.h"
#include "ai/ai.h"
#include "formations/formation_system.h"
#include "net/net.h"
#include "net/net_transport.h"
#include "ui/ui.h"
#include "environment/environment_system.h"
#include "character/character_model.h"
#include "character/modular_character.h"
#include "character/wardrobe_system.h"
#include "character/player_profile.h"
#include "ui/ui_wardrobe.h"
#include "sandbox/sandbox_core.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
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

  // Movement: 1s at 6.4 m/s (normal run speed).
  PlayerIntent intent = { 0 };
  intent.castSkillSlot = -1;
  intent.moveDir = (Vector2){ 1.0f, 0.0f };
  Control_Apply(&intent, 1.0f);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(agent)->position.x, base.x + 6.4f, 0.01f,
                                      "moved 6.4m in 1s", outReason, outReasonSize);

  // speedMult respected: 2x modifier → 12.8m in 1s.
  Entity_AddModifier(agent, 2.0f, 5.0f);
  float xBefore = Entity_GetAgent(agent)->position.x;
  Control_Apply(&intent, 1.0f);
  ok = ok && AutoTest_ExpectFloatNear(Entity_GetAgent(agent)->position.x - xBefore, 12.8f, 0.01f,
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

  // Taiji state remains active while mana > 0 and exits when mana is drained.
  int galaxyIdx = Skill_GetIndexByName("GALAXY_SPIRAL");
  ok = ok && AutoTest_ExpectTrue(galaxyIdx >= 0, "taiji skill registered", outReason, outReasonSize);
  Vector3 vortex = Entity_GetAgent(b)->position;
  ok = ok && AutoTest_ExpectTrue(CastSkill(galaxyIdx, a, base, vortex, (SkillParams){ .level = 1 }),
                                 "galaxy spiral cast went through", outReason, outReasonSize);

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

    int fireIdx = Skill_GetIndexByName("INFERNO_SURGE");
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

// The match (game_mode_loop test) pins SONG_QUAO + its ring-out bounds
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
      { "ICE_CRYSTAL_LANCE", 0 }, { "INFERNO_SURGE", 2 },
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
    int fire = Skill_GetIndexByName("INFERNO_SURGE");
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
  int phaseSkill = Skill_GetIndexByName("EARTH_PILLAR_PRISON");
  ok = ok && AutoTest_ExpectTrue(phaseSkill >= 0, "phase skill registered", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(!SkillManager_CanCast(phaseSkill, bossId), "boss cast engaged cooldown", outReason, outReasonSize);

  // Death through the normal entities path ends the fight.
  Entity_ApplyDamage(bossId, 1e9f, (Vector3){ 0 });
  Boss_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(!Boss_IsAlive() && Boss_GetPhase() == -1, "boss death ends fight", outReason, outReasonSize);

  Entity_ApplyDamage(hero, 1e9f, (Vector3){ 0 });

  // Test Boss Mộc Linh (3D skeletal animated boss)
  int mocId = Boss_Spawn(&BOSS_MOC_LINH, pos, TEAM_ENEMY);
  ok = ok && AutoTest_ExpectTrue(mocId >= 0, "moc linh spawned", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Boss_GetPhase() == 0, "moc linh starts in phase 0", outReason, outReasonSize);
  Boss_Draw(); // Tests drawVisual, 3D model loading and animation evaluation
  Entity_ApplyDamage(mocId, 1e9f, (Vector3){ 0 });
  Boss_Update(1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(!Boss_IsAlive() && Boss_GetPhase() == -1, "moc linh death ends fight", outReason, outReasonSize);

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

static AutoTestResult AutoTest_AtmosphereFogStep(int frameInCase, char *outReason, int outReasonSize)
{
  if (frameInCase > 0) return AUTOTEST_PASS;

  bool ok = true;

  // 1. Verify default AtmosphereProfile
  AtmosphereProfile defaultAtmos = Environment_GetAtmosphereProfile();
  ok &= AutoTest_ExpectTrue(defaultAtmos.optics.rayleighLMS.x > 0.005f,
                            "default Rayleigh LMS X is valid", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(defaultAtmos.optics.multipleScatteringAmp > 1.5f,
                            "default Multiple Scattering boost is > 1.5 (tuned for map)", outReason, outReasonSize);

  // 2. Test 2-way synchronization between legacy EnvFogConfig and new AtmosphereProfile
  EnvFogConfig originalFog = Environment_GetFogConfig();

  EnvFogConfig customFog = {
      .color = (Color){ 45, 55, 65, 255 },
      .start = 150.0f,
      .end = 850.0f,
      .density = 0.008f,
      .enabled = true
  };
  Environment_SetFogConfig(customFog);

  AtmosphereProfile syncedAtmos = Environment_GetAtmosphereProfile();
  ok &= AutoTest_ExpectTrue(syncedAtmos.color.r == 45 && syncedAtmos.color.g == 55,
                            "Atmosphere color synced from EnvFogConfig", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(syncedAtmos.start, 150.0f, 0.01f,
                                 "Atmosphere start distance synced", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(syncedAtmos.density.baseDensity, 0.008f, 0.0001f,
                                 "Atmosphere base density synced", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(syncedAtmos.enabled == true,
                            "Atmosphere enabled flag synced", outReason, outReasonSize);

  // 3. Test LocalFogVolume lifecycle and pool constraints
  FogVolume_ClearAll();
  ok &= AutoTest_ExpectTrue(FogVolume_GetActiveCount() == 0,
                            "FogVolume_ClearAll emptied pool", outReason, outReasonSize);

  int idPermanent = FogVolume_SpawnTransient((Vector3){ 10.0f, 0.0f, 10.0f }, 15.0f, (Color){ 200, 220, 255, 255 }, 0.5f, 0.0f); // permanent (lifetime <= 0)
  int idTransient = FogVolume_SpawnTransient((Vector3){ 0.0f, 0.0f, 0.0f }, 8.0f, (Color){ 100, 255, 100, 255 }, 0.8f, 0.5f);     // 0.5s lifetime
  ok &= AutoTest_ExpectTrue(idPermanent > 0 && idTransient > 0,
                            "Created 2 fog volumes with positive IDs", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(FogVolume_GetActiveCount() == 2,
                            "Fog volume active count is 2", outReason, outReasonSize);

  const LocalFogVolume *vPerm = FogVolume_GetById(idPermanent);
  ok &= AutoTest_ExpectTrue(vPerm != NULL && vPerm->density == 0.5f,
                            "Retrieved permanent fog volume correctly", outReason, outReasonSize);

  // Simulate 1.0s passing — transient volume should expire, permanent volume should remain
  Environment_Update(1.0f);
  ok &= AutoTest_ExpectTrue(FogVolume_GetActiveCount() == 1,
                            "Transient fog volume expired after lifetime", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(FogVolume_GetById(idTransient) == NULL,
                            "Expired volume lookup returns NULL", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(FogVolume_GetById(idPermanent) != NULL,
                            "Permanent volume still alive", outReason, outReasonSize);

  // Clean up
  FogVolume_Destroy(idPermanent);
  ok &= AutoTest_ExpectTrue(FogVolume_GetActiveCount() == 0,
                            "Destroyed remaining fog volume", outReason, outReasonSize);

  // Restore original fog config
  Environment_SetFogConfig(originalFog);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_CharacterAnimSystemStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  bool ok = true;
  ok &= AutoTest_ExpectTrue(CharacterModel_IsLoaded(), "CharacterModel is loaded", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(CharacterModel_GetTotalAnimCount() >= 13, "Total animation count >= 13", outReason, outReasonSize);

  // Check multi-variant slots
  int slashVariants = CharacterModel_GetVariantCount(CHAR_ANIM_SWORD_SLASH);
  ok &= AutoTest_ExpectTrue(slashVariants >= 2, "SWORD_SLASH has multiple variants", outReason, outReasonSize);

  int jumpDownVariants = CharacterModel_GetVariantCount(CHAR_ANIM_JUMP_DOWN);
  ok &= AutoTest_ExpectTrue(jumpDownVariants >= 1, "JUMP_DOWN slot has matched clip", outReason, outReasonSize);

  // Test state transitions & trigger by name
  CharacterAnimState testState;
  CharacterModel_ResetState(&testState);
  ok &= AutoTest_ExpectTrue(testState.currentSlot == CHAR_ANIM_IDLE, "ResetState sets IDLE", outReason, outReasonSize);

  CharacterModel_TriggerVariant(&testState, CHAR_ANIM_SWORD_SLASH, 1);
  ok &= AutoTest_ExpectTrue(testState.oneShotActive, "TriggerVariant activates oneShot", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(testState.activeVariant == 1, "TriggerVariant sets variant 1", outReason, outReasonSize);

  bool triggered = CharacterModel_TriggerByName(&testState, "jumping_down", 0.0f);
  ok &= AutoTest_ExpectTrue(triggered, "TriggerByName finds and triggers jumping_down", outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_ModularCharacterSystemStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  bool ok = true;
  ModularSystem_Init();

  // Test register item
  int wepIdx = ModularSystem_RegisterItem("kiem_thuan_thien", MODULAR_SLOT_WEAPON_R, false,
                                          "mixamorig:RightHand", (Vector3){0, 0.05f, 0}, (Vector3){0, 0, 0},
                                          "assets/characters/equipment/weapons/kiem_thuan_thien.glb");
  ok &= AutoTest_ExpectTrue(wepIdx >= 0, "RegisterItem returns valid index", outReason, outReasonSize);

  // Test find item
  int foundIdx = ModularSystem_FindItem("kiem_thuan_thien");
  ok &= AutoTest_ExpectTrue(foundIdx == wepIdx, "FindItem returns matching index", outReason, outReasonSize);

  // Test instance init and equip
  ModularInstance heroInst;
  ModularInstance_Init(&heroInst, MODULAR_TIER_HERO);
  ok &= AutoTest_ExpectTrue(heroInst.tier == MODULAR_TIER_HERO, "ModularInstance_Init sets hero tier", outReason, outReasonSize);

  bool equipped = ModularInstance_Equip(&heroInst, MODULAR_SLOT_WEAPON_R, "kiem_thuan_thien");
  ok &= AutoTest_ExpectTrue(equipped, "ModularInstance_Equip equips registered weapon", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(heroInst.loadout.itemIndices[MODULAR_SLOT_WEAPON_R] == wepIdx, "Loadout holds equipped weapon index", outReason, outReasonSize);

  // Test instance update
  ModularInstance_Update(&heroInst, 0.016f, false);
  ok &= AutoTest_ExpectTrue(heroInst.animState.currentSlot == CHAR_ANIM_IDLE, "ModularInstance_Update keeps valid slot", outReason, outReasonSize);

  // Test socket matrix calculation
  Matrix socketMat;
  bool gotSocket = ModularSystem_GetSocketMatrix(heroInst.position, heroInst.yaw, heroInst.scale,
                                                MODULAR_SLOT_WEAPON_R, MatrixIdentity(), &socketMat);
  ok &= AutoTest_ExpectTrue(gotSocket, "ModularSystem_GetSocketMatrix computes matrix", outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_PlayerWardrobeSystemStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  bool ok = true;
  Wardrobe_InitCatalog();
  ok &= AutoTest_ExpectTrue(Wardrobe_GetCatalogCount() >= 10, "Wardrobe catalog has items", outReason, outReasonSize);

  // Auto-discovery regression guard: dropping a .glb into <root>/equipment/<slot>/
  // must surface it in the wardrobe with no code or table edit. This broke once
  // because LoadDirectoryFilesEx's filter is not a shell glob ("*.glb" matched
  // nothing), and the failure is silent — an empty wardrobe, not an error.
  {
    const char *probeRoot = "/tmp/wuxing_equip_probe";
    MakeDirectory(probeRoot);
    MakeDirectory("/tmp/wuxing_equip_probe/equipment");
    MakeDirectory("/tmp/wuxing_equip_probe/equipment/torso");

    int probeSize = 0;
    unsigned char *probeBytes = LoadFileData("assets/characters/equipment/torso/ao_giao_linh.glb", &probeSize);
    bool wroteProbe = (probeBytes != NULL &&
                       SaveFileData("/tmp/wuxing_equip_probe/equipment/torso/probe_robe.glb", probeBytes, probeSize));
    if (probeBytes) UnloadFileData(probeBytes);

    int before = Wardrobe_GetCatalogCount();
    int discovered = Wardrobe_ScanEquipmentFolders("/tmp/wuxing_equip_probe/equipment");
    const WardrobeCatalogItem *probe = Wardrobe_GetCatalogItem("probe_robe");
    ok &= AutoTest_ExpectTrue(wroteProbe && discovered == 1,
                              "dropped .glb is auto-discovered from its equipment folder", outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(probe != NULL && probe->slot == MODULAR_SLOT_TORSO && probe->isSkinned,
                              "auto-discovered garment uses its folder's slot and skins", outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(Wardrobe_GetCatalogCount() == before + 1,
                              "auto-discovery adds exactly one catalog entry", outReason, outReasonSize);

    // Drop the probe again: it must not be registered twice (the scan reruns every boot).
    int rescanned = Wardrobe_ScanEquipmentFolders("/tmp/wuxing_equip_probe/equipment");
    ok &= AutoTest_ExpectTrue(rescanned == 0 && Wardrobe_GetCatalogCount() == before + 1,
                              "re-scanning is idempotent", outReason, outReasonSize);

    remove("/tmp/wuxing_equip_probe/equipment/torso/probe_robe.glb");
    Wardrobe_ShutdownCatalog();
    Wardrobe_InitCatalog();
    ok &= AutoTest_ExpectTrue(Wardrobe_GetCatalogItem("probe_robe") == NULL,
                              "catalog rebuild drops the test probe", outReason, outReasonSize);
  }

  // Initialize player wardrobe (starts with online starter set: body, torso, legs, boots)
  PlayerWardrobe pw;
  PlayerWardrobe_Init(&pw, "Hero_Customizer");
  ok &= AutoTest_ExpectTrue(pw.unlockedCount >= 4, "Player wardrobe starts with online starter set", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(PlayerWardrobe_GetCombatStance(&pw) == COMBAT_STANCE_UNARMED,
                            "Initial combat stance is UNARMED before finding weapon", outReason, outReasonSize);

  // Unlock and equip sword -> switches to SWORD stance
  PlayerWardrobe_UnlockItem(&pw, "kiem_thuan_thien");
  PlayerWardrobe_Equip(&pw, "kiem_thuan_thien");
  ok &= AutoTest_ExpectTrue(PlayerWardrobe_GetCombatStance(&pw) == COMBAT_STANCE_SWORD,
                            "Combat stance is SWORD when sword is equipped", outReason, outReasonSize);

  // Unequip weapon -> switches to Tay Khong (Unarmed)
  PlayerWardrobe_Unequip(&pw, MODULAR_SLOT_WEAPON_R);
  ok &= AutoTest_ExpectTrue(PlayerWardrobe_GetCombatStance(&pw) == COMBAT_STANCE_UNARMED,
                            "Combat stance switches to UNARMED when no weapon", outReason, outReasonSize);

  // Re-equip sword
  PlayerWardrobe_Equip(&pw, "kiem_thuan_thien");
  ok &= AutoTest_ExpectTrue(PlayerWardrobe_GetCombatStance(&pw) == COMBAT_STANCE_SWORD,
                            "Combat stance switches back to SWORD", outReason, outReasonSize);

  // Test unlocking newly discovered exploration item (online progression)
  bool unlockedNew = PlayerWardrobe_UnlockItem(&pw, "vo_kiem_son_mai");
  ok &= AutoTest_ExpectTrue(unlockedNew, "UnlockItem returns true for newly discovered item", outReason, outReasonSize);
  bool unlockedAgain = PlayerWardrobe_UnlockItem(&pw, "vo_kiem_son_mai");
  ok &= AutoTest_ExpectTrue(!unlockedAgain, "UnlockItem returns false when already owned", outReason, outReasonSize);

  // Test Full Outfit and occupied slots suppression
  bool unlockedOutfit = PlayerWardrobe_UnlockItem(&pw, "truong_bao_ngan_long");
  ok &= AutoTest_ExpectTrue(unlockedOutfit, "UnlockItem unlocks Full Outfit", outReason, outReasonSize);
  PlayerWardrobe_Equip(&pw, "truong_bao_ngan_long");
  ok &= AutoTest_ExpectTrue(strcmp(PlayerWardrobe_GetEquippedId(&pw, MODULAR_SLOT_FULL_OUTFIT), "truong_bao_ngan_long") == 0,
                            "Equipped full outfit", outReason, outReasonSize);

  // Test apply to 3D modular instance (verify occupied slot mask)
  ModularInstance inst;
  ModularInstance_Init(&inst, MODULAR_TIER_HERO);
  bool applied = PlayerWardrobe_ApplyToInstance(&pw, &inst);
  ok &= AutoTest_ExpectTrue(applied, "PlayerWardrobe_ApplyToInstance syncs to 3D instance", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue((inst.occupiedSlotsMask & MODULAR_OCCUPIED_TORSO) != 0,
                            "Full outfit suppresses occupied torso slot", outReason, outReasonSize);

  // Test combat attribute calculation (HP, MP, speed)
  WardrobeItemStats stats = PlayerWardrobe_GetTotalStats(&pw);
  ok &= AutoTest_ExpectTrue(stats.bonusHealth > 100.0f, "PlayerWardrobe_GetTotalStats aggregates health", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(stats.speedMultiplier >= 1.0f, "PlayerWardrobe_GetTotalStats aggregates speed", outReason, outReasonSize);

  // Test serialization (Save/Load)
  const char *testSavePath = "/tmp/test_player_wardrobe.json";
  bool saved = PlayerWardrobe_SaveToFile(&pw, testSavePath);
  ok &= AutoTest_ExpectTrue(saved, "PlayerWardrobe_SaveToFile writes JSON", outReason, outReasonSize);

  PlayerWardrobe loadedPw;
  bool loaded = PlayerWardrobe_LoadFromFile(&loadedPw, testSavePath);
  ok &= AutoTest_ExpectTrue(loaded, "PlayerWardrobe_LoadFromFile reads JSON", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(strcmp(PlayerWardrobe_GetEquippedId(&loadedPw, MODULAR_SLOT_WEAPON_R), "kiem_thuan_thien") == 0,
                            "Loaded wardrobe preserves equipped sword", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(strcmp(PlayerWardrobe_GetEquippedId(&loadedPw, MODULAR_SLOT_FULL_OUTFIT), "truong_bao_ngan_long") == 0,
                            "Loaded wardrobe preserves equipped full outfit", outReason, outReasonSize);

  // Test global wardrobe instance accessors
  ok &= AutoTest_ExpectTrue(Wardrobe_GetGlobalPlayer() != NULL, "Wardrobe_GetGlobalPlayer returns valid pointer", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(Wardrobe_GetGlobalModularInstance() != NULL, "Wardrobe_GetGlobalModularInstance returns valid pointer", outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_UIWardrobeStep(int frameInCase, char *outReason, int outReasonSize) {
  PlayerWardrobe *pw = Sandbox_GetPlayerWardrobe();
  ModularInstance *inst = Sandbox_GetPlayerModularInstance();
  if (!pw || !inst) {
    return AutoTest_ExpectTrue(false, "Sandbox player wardrobe initialized", outReason, outReasonSize) ? AUTOTEST_PASS : AUTOTEST_FAIL;
  }

  // UI_Wardrobe_Close() persists the loadout to assets/saves/player_wardrobe.json.
  // A test must not rewrite the player's real save, so the file is snapshotted on
  // the first frame and restored after the panel closes.
  static unsigned char *s_saveBackup = NULL;
  static int s_saveBackupSize = 0;
  static const char *kSavePath = "assets/saves/player_wardrobe.json";

  if (frameInCase == 0) {
    if (!s_saveBackup) s_saveBackup = LoadFileData(kSavePath, &s_saveBackupSize);
    PlayerWardrobe_UnlockItem(pw, "human_body");
    PlayerWardrobe_UnlockItem(pw, "ao_giao_linh");
    PlayerWardrobe_UnlockItem(pw, "quan_wuxia");
    PlayerWardrobe_UnlockItem(pw, "chien_hai_van_du");
    PlayerWardrobe_UnlockItem(pw, "kiem_thuan_thien");
    PlayerWardrobe_UnlockItem(pw, "vo_kiem_son_mai");
    PlayerWardrobe_UnlockItem(pw, "non_la_hiep_khach");
    PlayerWardrobe_Equip(pw, "human_body");
    PlayerWardrobe_Equip(pw, "ao_giao_linh");
    PlayerWardrobe_Equip(pw, "quan_wuxia");
    PlayerWardrobe_Equip(pw, "chien_hai_van_du");
    PlayerWardrobe_Unequip(pw, MODULAR_SLOT_WEAPON_R);
    PlayerWardrobe_Equip(pw, "vo_kiem_son_mai");
    PlayerWardrobe_Equip(pw, "non_la_hiep_khach");
    PlayerWardrobe_ApplyToInstance(pw, inst);

    AutoTest_SaveScreenshot("preview_sandbox_hud_button");
    UI_Wardrobe_Open(pw, inst);
    UI_Wardrobe_SetPreviewYaw(0.0f);
    return AUTOTEST_RUNNING;
  }

  if (frameInCase == 1) {
    AutoTest_SaveScreenshot("preview_wardrobe_ui");
    UI_Wardrobe_SetPreviewYaw(3.14159f);
    return AUTOTEST_RUNNING;
  }

  if (frameInCase == 2) {
    AutoTest_SaveScreenshot("preview_wardrobe_back");

    // Show the auto-discovered / rig-driven cloth garment: its tà áo, tay áo
    // thụng and dải lụa are simulated by the spring-bone solver.
    PlayerWardrobe_UnlockItem(pw, "ao_giao_linh_2");
    PlayerWardrobe_Equip(pw, "ao_giao_linh_2");
    PlayerWardrobe_ApplyToInstance(pw, inst);
    UI_Wardrobe_SetPreviewYaw(0.0f);
    return AUTOTEST_RUNNING;
  }

  if (frameInCase == 3) {
    AutoTest_SaveScreenshot("preview_wardrobe_cloth");

    // Gameplay draws the hero through CharacterModel_Draw() and never calls
    // ModularInstance_Update() for it, so that draw path has to step the cloth
    // solver itself. Without it the chains stay uninitialised in-game and every
    // cloth bone takes the rigid fallback — the robe splayed out like a cape.
    bool worldDrew = true;
    {
      SpringBone_Reset(&inst->springBones);
      CharacterModel_Draw(&inst->animState, inst->position, inst->yaw, inst->scale, WHITE);
      int liveSkirt = 0;
      for (int i = 0; i < inst->springBones.boneCount; i++) {
        if (inst->springBones.bones[i].initialized &&
            strstr(inst->springBones.bones[i].name, "skirt")) liveSkirt++;
      }
      worldDrew = AutoTest_ExpectTrue(liveSkirt > 0,
                                      "world draw path (CharacterModel_Draw) steps the cloth solver",
                                      outReason, outReasonSize);
    }

    UI_Wardrobe_Close();
    // main.c persists the global wardrobe again on exit, so the in-memory copy has
    // to go back to the player's own loadout too — restoring only the file would be
    // overwritten by the shutdown save.
    bool saveRestored = true;
    if (s_saveBackup && s_saveBackupSize > 0) {
      saveRestored = SaveFileData(kSavePath, s_saveBackup, s_saveBackupSize);
      PlayerWardrobe *globalPw = Wardrobe_GetGlobalPlayer();
      if (globalPw) saveRestored = PlayerWardrobe_LoadFromFile(globalPw, kSavePath) && saveRestored;
      UnloadFileData(s_saveBackup);
      s_saveBackup = NULL;
      s_saveBackupSize = 0;
    }
    return (worldDrew && saveRestored) ? AUTOTEST_PASS : AUTOTEST_FAIL;
  }

  return AUTOTEST_PASS;
}

// -----------------------------------------------------------------------------
// Cloth secondary motion (tà áo / tay áo thụng / dải lụa)
//
// Regression guard for the whole chain: `cloth_*` bones discovered from the
// garment rig -> spring-bone solve -> per-bone pose override -> re-skinned
// vertices. Each assertion fails if a link is broken, which is what "the robe
// is stiff / does not follow the animation / ignores wind" looks like.
// -----------------------------------------------------------------------------

static ModularInstance s_clothInst;
static unsigned int   s_clothVertexHash = 0;
static Vector3        s_clothTipStart = { 0 };
static bool           s_clothPrepared = false;

static unsigned int ClothTest_HashAllMeshes(const Model *model) {
    unsigned int h = 2166136261u;
    if (!model) return h;
    for (int m = 0; m < model->meshCount; m++) {
        const Mesh *mesh = &model->meshes[m];
        if (!mesh->animVertices) continue;
        int count = mesh->vertexCount * 3;
        for (int i = 0; i < count; i++) {
            float v = mesh->animVertices[i];
            unsigned int bits = 0;
            memcpy(&bits, &v, sizeof(bits));
            h = (h ^ bits) * 16777619u;
        }
    }
    return h;
}

// Bounds of the last CPU-skinned garment. Used to catch a garment that collapsed
// onto its parent bone (hem at the hips) or exploded sideways (sleeves splayed).
static void ClothTest_SkinnedBounds(const Model *model, float *outMinY, float *outMaxAbsX) {
    float minY = 1.0e9f, maxAbsX = 0.0f;
    if (model) {
        for (int m = 0; m < model->meshCount; m++) {
            const Mesh *mesh = &model->meshes[m];
            if (!mesh->animVertices) continue;
            for (int v = 0; v < mesh->vertexCount; v++) {
                float x = mesh->animVertices[v * 3 + 0];
                float y = mesh->animVertices[v * 3 + 1];
                if (y < minY) minY = y;
                if (fabsf(x) > maxAbsX) maxAbsX = fabsf(x);
            }
        }
    }
    if (outMinY) *outMinY = minY;
    if (outMaxAbsX) *outMaxAbsX = maxAbsX;
}

static Vector3 ClothTest_ZeroWind(Vector3 worldPos, void *userData) {
    (void)worldPos; (void)userData;
    return (Vector3){ 0.0f, 0.0f, 0.0f };
}

static Vector3 ClothTest_GaleWind(Vector3 worldPos, void *userData) {
    (void)worldPos; (void)userData;
    return (Vector3){ 7.0f, 0.0f, 0.0f };
}

static AutoTestResult AutoTest_ClothSecondaryMotionStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    bool ok = true;
    ModularSystem_Init();

    int bodyIdx = ModularSystem_RegisterItem("human_body", MODULAR_SLOT_BASE_BODY, true, NULL,
                                             (Vector3){ 0 }, (Vector3){ 0 },
                                             "assets/characters/bodies/human_body.glb");
    int robeIdx = ModularSystem_RegisterItem("ao_giao_linh_2", MODULAR_SLOT_TORSO, true, NULL,
                                             (Vector3){ 0 }, (Vector3){ 0 },
                                             "assets/characters/equipment/torso/ao_giao_linh_2.glb");
    const ModularItemDef *robe = ModularSystem_GetItem(robeIdx);
    ok &= AutoTest_ExpectTrue(bodyIdx >= 0 && robeIdx >= 0, "base body + ao_giao_linh_2 registered",
                              outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(robe && robe->loaded && robe->model.meshCount > 0,
                              "ao_giao_linh_2 GLB loads with skinned meshes", outReason, outReasonSize);

    ModularInstance_Init(&s_clothInst, MODULAR_TIER_HERO);
    ModularInstance_EquipIndex(&s_clothInst, MODULAR_SLOT_BASE_BODY, bodyIdx);
    ModularInstance_EquipIndex(&s_clothInst, MODULAR_SLOT_TORSO, robeIdx);
    s_clothInst.position = (Vector3){ 6.0f, 0.0f, 4.4f };
    s_clothInst.yaw = 0.0f;
    s_clothInst.scale = 1.0f;

    // 1. Chains come from the rig, not from a table.
    ok &= AutoTest_ExpectTrue(s_clothInst.springBones.boneCount >= 16,
                              "cloth chains discovered from the garment rig", outReason, outReasonSize);

    bool hasSleeve = false, hasSkirt = false, hasRibbon = false;
    for (int i = 0; i < s_clothInst.springBones.boneCount; i++) {
      const char *n = s_clothInst.springBones.bones[i].name;
      if (strstr(n, "sleeve")) hasSleeve = true;
      if (strstr(n, "skirt"))  hasSkirt = true;
      if (strstr(n, "ribbon")) hasRibbon = true;
    }
    ok &= AutoTest_ExpectTrue(hasSleeve, "ống tay áo (sleeve) chains present", outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(hasSkirt, "tà áo trước/sau (skirt) chains present", outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(hasRibbon, "dải lụa thắt lưng (ribbon) chains present", outReason, outReasonSize);

    // 2. Chains are parented: a 2-segment hem must reference its root.
    int chained = 0;
    for (int i = 0; i < s_clothInst.springBones.boneCount; i++) {
      if (s_clothInst.springBones.bones[i].parentSpring >= 0) chained++;
    }
    ok &= AutoTest_ExpectTrue(chained >= 8, "multi-segment cloth chains linked (incl. left sleeve)",
                              outReason, outReasonSize);

    // 3. Wind response: identical solvers, identical pose, different air.
    Vector3 noWindTip = { 0 }, galeTip = { 0 };
    bool gotNoWind = false, gotGale = false;
    {
      SpringBoneSystem calm, gale;
      calm = s_clothInst.springBones;
      gale = s_clothInst.springBones;
      SpringBone_Reset(&calm);
      SpringBone_Reset(&gale);
      for (int f = 0; f < 90; f++) {
        SpringBone_UpdateWithWind(&calm, &s_clothInst.animState, s_clothInst.position, 0.0f, 1.0f,
                                  1.0f / 60.0f, ClothTest_ZeroWind, NULL);
        SpringBone_UpdateWithWind(&gale, &s_clothInst.animState, s_clothInst.position, 0.0f, 1.0f,
                                  1.0f / 60.0f, ClothTest_GaleWind, NULL);
      }
      gotNoWind = SpringBone_GetBoneWorldPos(&calm, "cloth_skirt_f_c_02", NULL, &noWindTip);
      gotGale = SpringBone_GetBoneWorldPos(&gale, "cloth_skirt_f_c_02", NULL, &galeTip);
    }
    ok &= AutoTest_ExpectTrue(gotNoWind && gotGale, "front hem tip is queryable", outReason, outReasonSize);
    float windDeltaX = galeTip.x - noWindTip.x;
    ok &= AutoTest_ExpectTrue(fabsf(windDeltaX) > 0.02f,
                              "hem trails downwind (wind actually drives the cloth)", outReason, outReasonSize);

    // 4. The cloth frame is anchored to the skeleton: the same pose at a
    //    different character yaw must give the same MODEL-space head.
    Vector3 headYaw0 = { 0 }, headYaw90 = { 0 }, dirYaw0 = { 0 }, dirYaw90 = { 0 };
    {
      SpringBoneSystem a, b;
      a = s_clothInst.springBones;
      b = s_clothInst.springBones;
      SpringBone_Reset(&a);
      SpringBone_Reset(&b);
      for (int f = 0; f < 45; f++) {
        SpringBone_UpdateWithWind(&a, &s_clothInst.animState, s_clothInst.position, 0.0f, 1.0f,
                                  1.0f / 60.0f, ClothTest_ZeroWind, NULL);
        SpringBone_UpdateWithWind(&b, &s_clothInst.animState, s_clothInst.position, PI * 0.5f, 1.0f,
                                  1.0f / 60.0f, ClothTest_ZeroWind, NULL);
      }
      SpringBone_GetBoneModelPose(&a, "cloth_skirt_f_c_02", &headYaw0, &dirYaw0);
      SpringBone_GetBoneModelPose(&b, "cloth_skirt_f_c_02", &headYaw90, &dirYaw90);
      SpringBone_GetBoneWorldPos(&a, "cloth_skirt_f_c_02", NULL, &s_clothTipStart);
    }
    ok &= AutoTest_ExpectTrue(Vector3Distance(headYaw0, headYaw90) < 0.02f,
                              "cloth heads stay in model space across character yaw",
                              outReason, outReasonSize);
    ok &= AutoTest_ExpectTrue(Vector3DotProduct(dirYaw0, dirYaw90) > 0.98f,
                              "cloth rest direction follows the hips, not the world",
                              outReason, outReasonSize);

    // 5. Skin the garment once so the animVertices buffer is populated.
    CharacterModel_DrawCustomCloth(robe->model, &s_clothInst.animState,
                                   s_clothInst.position, s_clothInst.yaw, s_clothInst.scale,
                                   WHITE, &s_clothInst.springBones);
    s_clothVertexHash = ClothTest_HashAllMeshes(&robe->model);
    s_clothPrepared = true;

    // 6. Rigid fallback (no solver attached): every cloth bone must ride its
    //    parent at its authored local offset. Dropping that conjugation used to
    //    collapse the robe onto the hips — a floor-length tà áo became a band at
    //    the waist with the legs exposed (character/docs/LANDMINES.md §13).
    {
      CharacterModel_DrawCustomCloth(robe->model, &s_clothInst.animState,
                                     s_clothInst.position, s_clothInst.yaw, s_clothInst.scale,
                                     WHITE, NULL);
      float minY = 0.0f, maxAbsX = 0.0f;
      ClothTest_SkinnedBounds(&robe->model, &minY, &maxAbsX);
      char detail[160];
      snprintf(detail, sizeof(detail), "hem y=%.2f (want <0.45), half-width x=%.2f (want <1.0)",
               minY, maxAbsX);
      ok &= AutoTest_ExpectTrue(minY < 0.45f && maxAbsX < 1.0f,
                                detail, outReason, outReasonSize);
    }

    return ok ? AUTOTEST_RUNNING : AUTOTEST_FAIL;
  }

  if (!s_clothPrepared) return AUTOTEST_FAIL;

  // Let the solver settle while the idle animation plays, re-skinning each frame.
  ModularInstance_Update(&s_clothInst, 1.0f / 60.0f, false);
  const ModularItemDef *robe = ModularSystem_GetItem(s_clothInst.loadout.itemIndices[MODULAR_SLOT_TORSO]);
  if (robe && robe->loaded) {
    CharacterModel_DrawCustomCloth(robe->model, &s_clothInst.animState,
                                   s_clothInst.position, s_clothInst.yaw, s_clothInst.scale,
                                   WHITE, &s_clothInst.springBones);
  }

  if (frameInCase < 124) return AUTOTEST_RUNNING;

  bool ok = true;
  Vector3 tipNow = { 0 };
  bool gotTip = SpringBone_GetBoneWorldPos(&s_clothInst.springBones, "cloth_skirt_f_c_02", NULL, &tipNow);
  ok &= AutoTest_ExpectTrue(gotTip, "hem tip still alive after simulation", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(Vector3Distance(tipNow, s_clothTipStart) > 0.005f,
                            "hem moves while the animation plays", outReason, outReasonSize);

  unsigned int hashNow = ClothTest_HashAllMeshes(robe ? &robe->model : NULL);
  ok &= AutoTest_ExpectTrue(hashNow != s_clothVertexHash,
                            "garment vertices are re-skinned from the cloth pose",
                            outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_PvEPvPBalanceStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  int a = Entity_SpawnAgent((Vector3){ 6.0f, 0.0f, 4.4f }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(a >= 0, "spawned balance test agent", outReason, outReasonSize);

  AgentPvEBonuses pve = {
    .bonusMaxHealth  = 60.0f,
    .bonusMaxMana    = 40.0f,
    .moveSpeedMult   = 1.20f,
    .damageOutMult   = 1.50f,
    .damageTakenMult = 0.50f,
    .manaRegenMult   = 2.00f,
  };
  Entity_SetPvEBonuses(a, &pve);
  const Agent *pa = Entity_GetAgent(a);
  ok &= AutoTest_ExpectFloatNear(pa->maxHealth, 160.0f, 0.01f, "PvE maxHealth boosted to 160", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(pa->maxMana, 140.0f, 0.01f, "PvE maxMana boosted to 140", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(Entity_GetSpeedMult(a), 1.20f, 0.01f, "PvE speedMult 1.20", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(Entity_GetDamageOutMult(a), 1.50f, 0.01f, "PvE dmgOutMult 1.50", outReason, outReasonSize);

  // Incoming damage halved by PvE armor
  Entity_ApplyDamage(a, 20.0f, (Vector3){ 0 });
  ok &= AutoTest_ExpectFloatNear(Entity_GetAgent(a)->health, 150.0f, 0.01f, "PvE damageTakenMult halved 20->10", outReason, outReasonSize);

  // Switch to PvP -> 100% fair-play normalization
  Entity_SetPvEBonuses(a, NULL);
  pa = Entity_GetAgent(a);
  ok &= AutoTest_ExpectFloatNear(pa->maxHealth, 100.0f, 0.01f, "PvP maxHealth normalized to 100", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(pa->maxMana, 100.0f, 0.01f, "PvP maxMana normalized to 100", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(Entity_GetSpeedMult(a), 1.0f, 0.01f, "PvP speedMult normalized to 1.0", outReason, outReasonSize);
  ok &= AutoTest_ExpectFloatNear(Entity_GetDamageOutMult(a), 1.0f, 0.01f, "PvP dmgOutMult normalized to 1.0", outReason, outReasonSize);

  // Skill level progression: Lv.1 -> Lv.2 in PvE, but PvP always returns 1
  int testSkill = 0;
  PlayerProfile *prof = PlayerProfile_GetGlobal();
  uint8_t savedLvl = prof->skillLevel[testSkill];
  uint32_t savedXp = prof->skillXp[testSkill];
  prof->skillLevel[testSkill] = 1;
  prof->skillXp[testSkill] = 0;
  bool leveled = PlayerProfile_AddSkillXp(testSkill, 150);
  ok &= AutoTest_ExpectTrue(leveled && prof->skillLevel[testSkill] == 2,
                            "skill leveled up to 2 in profile", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(PlayerProfile_GetEffectiveSkillLevel(testSkill, false) == 2,
                            "PvE effective skill level is 2", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(PlayerProfile_GetEffectiveSkillLevel(testSkill, true) == 1,
                            "PvP effective skill level normalized to 1", outReason, outReasonSize);
  prof->skillLevel[testSkill] = savedLvl;
  prof->skillXp[testSkill] = savedXp;

  Entity_ApplyDamage(a, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_LoadoutFiveSlotsStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  int a = Entity_SpawnAgent((Vector3){ 6.0f, 0.0f, 4.4f }, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(a >= 0, "spawned 5-slot loadout agent", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(AGENT_SKILL_SLOTS == 5, "AGENT_SKILL_SLOTS is 5", outReason, outReasonSize);

  // 2 Âm (Water, Wood) + 2 Dương (Fire, Metal) + 1 Trung Hòa (Earth) -> Thái Cực!
  Entity_SetEquippedSkill(a, 0, 10, 0); // Water (Âm)
  Entity_SetEquippedSkill(a, 1, 11, 1); // Wood  (Âm)
  Entity_SetEquippedSkill(a, 2, 12, 2); // Fire  (Dương)
  Entity_SetEquippedSkill(a, 3, 13, 4); // Metal (Dương)
  Entity_SetEquippedSkill(a, 4, 14, 3); // Earth (Trung Hòa)
  ok &= AutoTest_ExpectTrue(Entity_IsTaijiActive(a), "5-slot 2am+2duong+1tho enters taiji", outReason, outReasonSize);

  // Swap Earth for Fire -> 3 Dương + 2 Âm -> exits Thái Cực, Fire majority
  Entity_SetEquippedSkill(a, 4, 15, 2);
  ok &= AutoTest_ExpectTrue(!Entity_IsTaijiActive(a), "unbalanced 5-slot exits taiji", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(Entity_GetAgent(a)->currentElement == 2, "5-slot majority = Fire", outReason, outReasonSize);

  Entity_ApplyDamage(a, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DaTauQuestStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  PlayerProfile *prof = PlayerProfile_GetGlobal();
  int savedGold = prof->gold;
  int savedPts = prof->daTauPoints;
  int savedStreak = prof->daTauStreak;
  int savedTotal = prof->daTauTotalCompleted;

  DaTau_Init(0);
  const DaTauQuest *q = DaTau_GetCurrent();
  bool ok = AutoTest_ExpectTrue(q->active && q->targetCount > 0, "datau quest rolled", outReason, outReasonSize);

  int needed = q->targetCount;
  bool completed = false;
  for (int i = 0; i < needed + 5 && !completed; i++) {
    if (q->type == DATAU_FORAGE_COLLECT) {
      completed = DaTau_OnForageCollected(1);
    } else if (q->type == DATAU_ZONE_HOLD) {
      DaTau_TickZoneHold(1.0f, true);
      completed = (prof->daTauStreak > savedStreak);
    } else {
      completed = DaTau_OnMonsterKilled(MONSTER_RANGED_CASTER, 0, true);
      if (!completed) completed = DaTau_OnMonsterKilled(MONSTER_ELITE_GUARD, 0, true);
    }
  }
  ok &= AutoTest_ExpectTrue(completed, "datau quest completed", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(prof->daTauStreak == savedStreak + 1, "streak incremented", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(prof->gold > savedGold && prof->daTauPoints > savedPts,
                            "gold and points rewarded", outReason, outReasonSize);

  prof->gold = savedGold;
  prof->daTauPoints = savedPts;
  prof->daTauStreak = savedStreak;
  prof->daTauTotalCompleted = savedTotal;
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DualBossEscortStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  Boss_ClearAll();
  Vector3 c = { 6.0f, 0.0f, 4.4f };
  int b0 = Boss_SpawnSlot(0, &BOSS_MOC_LINH, (Vector3){ 2.0f, 0.0f, 4.4f }, TEAM_ALLY);
  int b1 = Boss_SpawnSlot(1, &BOSS_HAC_DIEN_TON_GIA, (Vector3){ 10.0f, 0.0f, 4.4f }, TEAM_ENEMY);
  bool ok = AutoTest_ExpectTrue(b0 >= 0 && b1 >= 0, "both bosses spawned", outReason, outReasonSize);

  BossRoute r0 = { .waypoints = { c }, .waypointCount = 1, .marchSpeedMps = 4.0f };
  BossRoute r1 = { .waypoints = { c }, .waypointCount = 1, .marchSpeedMps = 4.0f };
  Boss_SetRoute(0, &r0);
  Boss_SetRoute(1, &r1);

  float x0Before = Entity_GetAgent(b0)->position.x;
  float x1Before = Entity_GetAgent(b1)->position.x;
  for (int f = 0; f < 30; f++) {
    Boss_Update(1.0f / 60.0f);
    Entity_Update(1.0f / 60.0f);
  }
  float x0After = Entity_GetAgent(b0)->position.x;
  float x1After = Entity_GetAgent(b1)->position.x;
  ok &= AutoTest_ExpectTrue(x0After > x0Before + 0.5f && x1After < x1Before - 0.5f,
                            "both bosses march toward convergence", outReason, outReasonSize);

  Boss_ClearAll();
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_SkillXpAndPvpNullificationStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  PlayerProfile *prof = PlayerProfile_GetGlobal();
  int testSkill = 0;
  uint8_t savedLv = prof->skillLevel[testSkill];
  uint32_t savedXp = prof->skillXp[testSkill];

  prof->skillLevel[testSkill] = 10;
  prof->skillXp[testSkill] = 0;

  int lvlPve = 0, qtyPve = 0, lvlPvp = 0, qtyPvp = 0;
  float dmgPve = 0.0f, dmgPvp = 0.0f;
  PlayerProfile_GetSkillScaling(testSkill, false, &lvlPve, NULL, &qtyPve, NULL, &dmgPve, NULL);
  PlayerProfile_GetSkillScaling(testSkill, true,  &lvlPvp, NULL, &qtyPvp, NULL, &dmgPvp, NULL);

  bool ok = AutoTest_ExpectTrue(lvlPve == 10 && qtyPve == 4 && fabsf(dmgPve - 2.62f) < 0.01f,
                                "PvE Lv.10 scaling active", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(lvlPvp == 1 && qtyPvp == 1 && fabsf(dmgPvp - 1.0f) < 0.01f,
                            "PvP normalized to Lv.1", outReason, outReasonSize);

  int attacker = Entity_SpawnAgent((Vector3){ 6.0f, 0.0f, 4.4f }, 100.0f, 3, TEAM_ALLY, ARCH_HERO);
  int minion   = Entity_SpawnAgent((Vector3){ 8.0f, 0.0f, 4.4f }, 100.0f, 0, TEAM_ENEMY, ARCH_MINION);
  int heroTgt  = Entity_SpawnAgent((Vector3){ 7.0f, 0.0f, 4.4f }, 100.0f, 2, TEAM_ALLY, ARCH_HERO);
  ok &= AutoTest_ExpectTrue(attacker >= 0 && minion >= 0 && heroTgt >= 0, "agents spawned", outReason, outReasonSize);

  AgentPvEBonuses bonus = { .damageOutMult = 1.5f, .damageTakenMult = 0.5f };
  Entity_SetPvEBonuses(attacker, &bonus);
  Entity_SetPvEBonuses(heroTgt, &bonus);
  Entity_SetLastCast(attacker, testSkill, dmgPve);

  // 1) Vs Monster: receives full skill level (2.62x) * gear (1.5x) = 39.3 dmg
  Entity_ApplyDamageFrom(minion, attacker, 10.0f, 0.0f, (Vector3){ 0 });
  float minionLost = 100.0f - Entity_GetAgent(minion)->health;
  ok &= AutoTest_ExpectTrue(fabsf(minionLost - 39.3f) < 0.1f, "monster takes full PvE skill+gear dmg", outReason, outReasonSize);

  // 2) Vs Real Player with Duel Flag ON: normalized to base 10.0 dmg
  Entity_SetDuelFlag(attacker, true);
  Entity_SetDuelFlag(heroTgt, true);
  Entity_ApplyDamageFrom(heroTgt, attacker, 10.0f, 0.0f, (Vector3){ 0 });
  float heroLost = 100.0f - Entity_GetAgent(heroTgt)->health;
  ok &= AutoTest_ExpectTrue(fabsf(heroLost - 10.0f) < 0.01f, "PK target ignores PvE skill+gear dmg", outReason, outReasonSize);

  // 3) Safe Zone blocks PK damage completely
  Entity_SetSafeZone(heroTgt, true);
  Entity_ApplyDamageFrom(heroTgt, attacker, 10.0f, 0.0f, (Vector3){ 0 });
  ok &= AutoTest_ExpectTrue(fabsf(Entity_GetAgent(heroTgt)->health - 90.0f) < 0.01f,
                            "safe zone blocks PK damage", outReason, outReasonSize);

  Entity_ApplyDamage(attacker, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(minion, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(heroTgt, 1e9f, (Vector3){ 0 });
  prof->skillLevel[testSkill] = savedLv;
  prof->skillXp[testSkill] = savedXp;
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DesertOpenWorldLoopStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  OW_Init(OWRealm_GetDesertEarth(), false);
  bool ok = AutoTest_ExpectTrue(OW_GetZoneAt((Vector3){ 170.68f, 0, 230.41f }) == 0,
                                "oasis safe zone detected", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(OW_GetZoneAt((Vector3){ 135.0f, 0, 195.0f }) == 1,
                            "starter dunes zone detected", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(OW_GetZoneAt((Vector3){ 94.1f, 0, 150.0f }) == 2,
                            "relic canyon zone detected", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(OW_GetZoneAt((Vector3){ 65.0f, 0, 95.0f }) == 3,
                            "boss badlands zone detected", outReason, outReasonSize);

  int before = OW_GetActiveNodeCount();
  ok &= AutoTest_ExpectTrue(before >= 10, "desert nodes initialized", outReason, outReasonSize);

  bool harvested = OW_TryInteractNearest((Vector3){ 122.0f, 0.0f, 178.0f });
  ok &= AutoTest_ExpectTrue(harvested && OW_GetActiveNodeCount() == before - 1,
                            "buried chest harvested", outReason, outReasonSize);

  OW_TriggerSandstormEvent();
  ok &= AutoTest_ExpectTrue(OW_GetActiveNodeCount() == before,
                            "sandstorm revealed golden chest", outReason, outReasonSize);

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DaTauNpcCycleAndWispStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  PlayerProfile *prof = PlayerProfile_GetGlobal();
  int savedGold = prof->gold;
  int savedPts = prof->daTauPoints;
  int savedStreak = prof->daTauStreak;
  int savedTotal = prof->daTauTotalCompleted;

  OW_Init(OWRealm_GetDesertEarth(), false);
  const DaTauQuest *q = DaTau_GetCurrent();
  bool ok = AutoTest_ExpectTrue(q->npcFlowEnabled && q->phase == DATAU_PHASE_WAIT_ACCEPT,
                                "starts waiting for NPC accept", outReason, outReasonSize);

  Vector3 npcPos = OW_GetDaTauNpcPos();
  Vector3 guide1 = OW_GetGuideTarget((Vector3){ 150.0f, 0.0f, 210.0f });
  ok &= AutoTest_ExpectTrue(fabsf(guide1.x - npcPos.x) < 0.2f && fabsf(guide1.z - npcPos.z) < 0.2f,
                            "wisp points to Da Tau NPC when WAIT_ACCEPT", outReason, outReasonSize);

  ok &= AutoTest_ExpectTrue(DaTau_AcceptFromNPC() && DaTau_GetCurrent()->phase == DATAU_PHASE_IN_PROGRESS,
                            "NPC accept transitions to IN_PROGRESS", outReason, outReasonSize);
  OW_SyncQuestEncounter();

  Vector3 guide2 = OW_GetGuideTarget((Vector3){ 160.0f, 0.0f, 222.0f });
  ok &= AutoTest_ExpectTrue(hypotf(guide2.x - npcPos.x, guide2.z - npcPos.z) > 10.0f,
                            "wisp points away from NPC to quest objective", outReason, outReasonSize);

  bool ready = DaTau_AdvanceObjective(DaTau_GetCurrent()->targetCount);
  ok &= AutoTest_ExpectTrue(ready && DaTau_GetCurrent()->phase == DATAU_PHASE_READY_RETURN,
                            "completing objective transitions to READY_RETURN", outReason, outReasonSize);

  Vector3 guide3 = OW_GetGuideTarget((Vector3){ 120.0f, 0.0f, 170.0f });
  ok &= AutoTest_ExpectTrue(fabsf(guide3.x - npcPos.x) < 0.2f && fabsf(guide3.z - npcPos.z) < 0.2f,
                            "wisp points back to Da Tau NPC for turn-in", outReason, outReasonSize);

  ok &= AutoTest_ExpectTrue(DaTau_TurnInToNPC() && DaTau_GetCurrent()->phase == DATAU_PHASE_WAIT_ACCEPT,
                            "NPC turn-in grants reward and rolls next WAIT_ACCEPT quest", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(prof->gold > savedGold && prof->daTauStreak == savedStreak + 1,
                            "gold & streak updated on turn-in", outReason, outReasonSize);

  prof->gold = savedGold;
  prof->daTauPoints = savedPts;
  prof->daTauStreak = savedStreak;
  prof->daTauTotalCompleted = savedTotal;
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_NetP2PExploreOptStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase > 0) return AUTOTEST_PASS;

  int h  = Entity_SpawnAgent((Vector3){ 10.0f, 0.0f, 10.0f }, 100.0f, 3, TEAM_ALLY, ARCH_HERO);
  int m1 = Entity_SpawnAgent((Vector3){ 25.0f, 0.0f, 10.0f }, 50.0f,  3, TEAM_ENEMY, ARCH_MINION);
  int m2 = Entity_SpawnAgent((Vector3){ 120.0f, 0.0f, 10.0f }, 50.0f, 3, TEAM_ENEMY, ARCH_MINION); // > 55m
  int b1 = Entity_SpawnAgent((Vector3){ 140.0f, 0.0f, 10.0f }, 500.0f, 3, TEAM_ENEMY, ARCH_BOSS);   // > 55m but BOSS!
  Entity_SetDuelFlag(h, true);

  unsigned char buf[1024];
  int bytes = Net_PackAgentSnapshotFor((Vector3){ 10.0f, 0.0f, 10.0f }, NET_INTEREST_RADIUS, buf, (int)sizeof(buf));
  bool ok = AutoTest_ExpectTrue(bytes > 0 && bytes <= 4 + 4 * 18, "quantized packet <= 18B/agent", outReason, outReasonSize);

  NetAgentState out[16];
  int n = Net_UnpackAgentSnapshot(out, 16, buf, bytes);
  bool hasHero = false, hasNearMinion = false, hasFarMinion = false, hasFarBoss = false, duelPreserved = false;
  for (int i = 0; i < n; i++) {
    if (out[i].agentId == h) {
      hasHero = true;
      duelPreserved = (out[i].flags & 8) != 0;
    }
    if (out[i].agentId == m1) hasNearMinion = true;
    if (out[i].agentId == m2) hasFarMinion = true;
    if (out[i].agentId == b1) hasFarBoss = true;
  }
  ok &= AutoTest_ExpectTrue(hasHero && duelPreserved, "hero & duelFlag synced", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(hasNearMinion && !hasFarMinion, "interest culling filters >55m minions", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(hasFarBoss, "distant boss always synced", outReason, outReasonSize);

  Entity_ApplyDamage(h, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(m1, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(m2, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(b1, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DesertSandboxAndWeatherStep(int frameInCase, char *outReason, int outReasonSize)
{
  (void)frameInCase;
  const OWRealmDef *realm = OWRealm_GetDesertEarth();
  OW_Init(realm, false);

  // 1. 3-state PK rules: PEACE blocks, SPAR clamps at 1 HP, SLAUGHTER attacks non-safe targets
  int h1 = Entity_SpawnAgent((Vector3){ 120.0f, 0.0f, 170.0f }, 100.0f, 3, TEAM_ALLY, ARCH_HERO);
  int h2 = Entity_SpawnAgent((Vector3){ 123.0f, 0.0f, 170.0f }, 100.0f, 3, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(h1 >= 0 && h2 >= 0, "spawned two open-world heroes", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(!Entity_CanDamageTarget(h1, h2), "PEACE blocks hero damage", outReason, outReasonSize);

  Entity_SetPkMode(h1, PK_MODE_SPAR);
  Entity_SetPkMode(h2, PK_MODE_SPAR);
  ok &= AutoTest_ExpectTrue(Entity_CanDamageTarget(h1, h2), "SPAR allows mutual spar damage", outReason, outReasonSize);
  Entity_ApplyDamageFrom(h2, h1, 500.0f, 1.0f, (Vector3){ 0 });
  const Agent *a2 = Entity_GetAgent(h2);
  int attId = -1;
  bool sparEnded = Entity_ConsumeSparDefeated(h2, &attId);
  ok &= AutoTest_ExpectTrue(a2 != NULL && a2->health >= 0.99f && a2->health <= 1.01f && sparEnded && attId == h1,
                            "SPAR non-lethal clamp holds at 1.0 HP and flags sparDefeated", outReason, outReasonSize);

  Entity_SetPkMode(h1, PK_MODE_SLAUGHTER);
  Entity_SetPkMode(h2, PK_MODE_PEACE);
  ok &= AutoTest_ExpectTrue(Entity_CanDamageTarget(h1, h2), "SLAUGHTER can attack non-safe hero", outReason, outReasonSize);
  Entity_SetSafeZone(h2, true);
  ok &= AutoTest_ExpectTrue(!Entity_CanDamageTarget(h1, h2), "SafeZone blocks SLAUGHTER", outReason, outReasonSize);

  Entity_ApplyDamage(h1, 1e9f, (Vector3){ 0 });
  Entity_ApplyDamage(h2, 1e9f, (Vector3){ 0 });

  // 2. Data-driven Weather cycling (4 desert profiles)
  ok &= AutoTest_ExpectTrue(OWWeather_GetProfileCount() == 4, "Desert realm defines 4 weather profiles", outReason, outReasonSize);
  OWWeather_SetWeather(1, 30.0f);
  ok &= AutoTest_ExpectTrue(OWWeather_GetCurrentIndex() == 1 && OWWeather_GetMoveSpeedMult() > 1.2f,
                            "Sandstorm grants tailwind speed bonus", outReason, outReasonSize);
  OWWeather_SetWeather(2, 30.0f);
  ok &= AutoTest_ExpectTrue(OWWeather_GetCurrentIndex() == 2 && OWWeather_GetXpMult() >= 1.49f,
                            "Mirage weather grants 1.5x skill XP multiplier", outReason, outReasonSize);

  // 3. Data-driven Spawner presets & Clear
  PlayerEntity tmpPlayer = { 0 };
  tmpPlayer.agentId = Entity_SpawnAgent((Vector3){ 120.0f, 0.0f, 170.0f }, 100.0f, 3, TEAM_ALLY, ARCH_HERO);
  ok &= AutoTest_ExpectTrue(OWSandbox_TriggerPreset(&tmpPlayer, 0), "Spawn monster pack preset succeeds", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(OWSandbox_GetActiveCustomSpawnCount() == realm->spawnerPresets[0].count, "preset monster pack spawned", outReason, outReasonSize);
  OWSandbox_ClearCustomSpawns();
  ok &= AutoTest_ExpectTrue(OWSandbox_GetActiveCustomSpawnCount() == 0, "ClearCustomSpawns removes all spawned monsters", outReason, outReasonSize);
  Entity_ApplyDamage(tmpPlayer.agentId, 1e9f, (Vector3){ 0 });

  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_SpatialHashEquivalenceStep(int frameInCase, char *outReason, int outReasonSize)
{
  (void)frameInCase;
  Entity_Init();
  int ids[200];
  int spawned = 0;
  for (int i = 0; i < 200; i++) {
    Vector3 p = {
      (float)((i * 73) % 700) + 20.0f,
      0.0f,
      (float)((i * 137) % 1100) + 20.0f
    };
    AgentTeam team = (i % 3 == 0) ? TEAM_ALLY : TEAM_ENEMY;
    ids[i] = Entity_SpawnAgent(p, 50.0f, i % 5, team, ARCH_MINION);
    if (ids[i] >= 0) spawned++;
  }
  bool ok = AutoTest_ExpectTrue(spawned == 200, "spawned 200 test agents", outReason, outReasonSize);

  for (int q = 0; q < 25; q++) {
    Vector3 center = { (float)((q * 97) % 700) + 20.0f, 0.0f, (float)((q * 193) % 1100) + 20.0f };
    float r = 2.0f + (float)(q % 6) * 3.5f;
    int got[MAX_AGENTS];
    int gotCount = Entity_GetNearbyTargets(center, r, got, MAX_AGENTS);

    int expected = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
      const Agent *a = Entity_GetAgent(i);
      if (!a) continue;
      float dx = a->position.x - center.x;
      float dz = a->position.z - center.z;
      if (dx * dx + dz * dz <= r * r) expected++;
    }
    if (gotCount != expected) {
      ok = AutoTest_ExpectTrue(false, "spatial hash query count matches brute-force", outReason, outReasonSize);
      break;
    }
  }
  for (int i = 0; i < 200; i++) {
    if (ids[i] >= 0) Entity_ApplyDamage(ids[i], 1e9f, (Vector3){ 0 });
  }
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_HordeSwarmAIStressStep(int frameInCase, char *outReason, int outReasonSize)
{
  (void)frameInCase;
  Entity_Init();
  AI_Init();
  Combat_Init();
  Entity_SetPlayableRect(0.0f, 300.0f, 0.0f, 300.0f);

  Vector3 safeC = { 200.0f, 0.0f, 200.0f };
  AI_SetSafeZone(safeC, 18.0f);
  int heroId = Entity_SpawnAgent((Vector3){ 100.0f, 0.0f, 100.0f }, 500.0f, 0, TEAM_ALLY, ARCH_HERO);
  bool ok = AutoTest_ExpectTrue(heroId >= 0, "spawned test hero", outReason, outReasonSize);

  AIHordeZoneDef hz = {
    .center = { 100.0f, 0.0f, 100.0f },
    .radius = 45.0f,
    .element = 3,
    .baseCount = 160,
    .maxConcurrent = 180,
    .waveIntervalSec = 4.0f,
    .waveBatchSize = 10,
    .leaperRatio = 0.2f,
    .spitterRatio = 0.15f,
    .eliteCount = 2
  };
  AI_RegisterHordeZone(&hz);

  double t0 = GetTime();
  for (int f = 0; f < 90; f++) {
    Combat_BeginFrame();
    Entity_Update(1.0f / 60.0f);
    AI_Update(1.0f / 60.0f);
    Combat_Update(1.0f / 60.0f);
  }
  double elapsedMs = (GetTime() - t0) * 1000.0;
  double msPerTick = elapsedMs / 90.0;

  AIMonsterRenderInfo rinfo[AI_MAX_CAMP_MONSTERS];
  int activeCount = AI_QueryMonsterRenderData(rinfo, AI_MAX_CAMP_MONSTERS);
  ok &= AutoTest_ExpectTrue(activeCount >= 120, "horde zone woke >= 120 simultaneous monsters", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue(msPerTick < 6.0, "90-frame horde AI+Spatial+Combat tick under budget", outReason, outReasonSize);

  // Check Boids separation: average nearest-neighbor distance > 0.65m
  double nnSum = 0.0;
  for (int i = 0; i < activeCount; i++) {
    float bestSq = 1e9f;
    for (int j = 0; j < activeCount; j++) {
      if (i == j) continue;
      float dx = rinfo[i].pos.x - rinfo[j].pos.x;
      float dz = rinfo[i].pos.z - rinfo[j].pos.z;
      float d2 = dx * dx + dz * dz;
      if (d2 < bestSq) bestSq = d2;
    }
    nnSum += sqrt((double)bestSq);
  }
  double avgNN = (activeCount > 1) ? (nnSum / (double)activeCount) : 1.0;
  ok &= AutoTest_ExpectTrue(avgNN > 0.65, "boids separation prevents stacking collapse", outReason, outReasonSize);

  AI_ClearHordeZones();
  AI_ClearMonsterCamps();
  Entity_ClearPlayableRect();
  Entity_ApplyDamage(heroId, 1e9f, (Vector3){ 0 });
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

static AutoTestResult AutoTest_DemDualScaleTiledStep(int frameInCase, char *outReason, int outReasonSize)
{
  (void)frameInCase;
  int sqIdx = -1;
  for (int i = 0; i < MapManager_GetCount(); i++) {
    if (strcmp(MapManager_GetName(i), "SONG_QUAO") == 0) { sqIdx = i; break; }
  }
  bool ok = AutoTest_ExpectTrue(sqIdx >= 0, "SONG_QUAO map registered", outReason, outReasonSize);
  if (sqIdx < 0) return AUTOTEST_FAIL;

  MapManager_SetScaleMode(MAP_SCALE_MOBA);
  MapManager_SetActiveIndex(sqIdx);
  Vector3 mobaCenter; float mobaRadius;
  MapManager_GetActiveBounds(&mobaCenter, &mobaRadius);

  MapManager_SetScaleMode(MAP_SCALE_OPEN_WORLD);
  MapManager_SetActiveIndex(sqIdx);
  Vector3 owCenter; float owRadius;
  MapManager_GetActiveBounds(&owCenter, &owRadius);

  ok &= AutoTest_ExpectTrue(owRadius >= mobaRadius * 3.8f, "Open World scale expands bounds 4x over MOBA", outReason, outReasonSize);
  float rMinX, rMaxX, rMinZ, rMaxZ;
  ok &= AutoTest_ExpectTrue(MapManager_GetActivePlayableRect(&rMinX, &rMaxX, &rMinZ, &rMaxZ), "Playable rect active", outReason, outReasonSize);
  ok &= AutoTest_ExpectTrue((rMaxX - rMinX) > 650.0f && (rMaxZ - rMinZ) > 1000.0f, "Open World spans > 650m x 1000m", outReason, outReasonSize);

  Vector3 sp = MapManager_GetActiveSpawnPoint();
  ok &= AutoTest_ExpectTrue(sp.y > -10.0f, "Open World spawn point has valid ground height", outReason, outReasonSize);

  // Restore MOBA scale for remaining tests
  MapManager_SetScaleMode(MAP_SCALE_MOBA);
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
  AutoTest_Register("atmosphere_and_fog_volumes", AutoTest_AtmosphereFogStep, 5);
  AutoTest_Register("character_animation_system", AutoTest_CharacterAnimSystemStep, 5);
  AutoTest_Register("modular_character_system", AutoTest_ModularCharacterSystemStep, 5);
  AutoTest_Register("player_wardrobe_system", AutoTest_PlayerWardrobeSystemStep, 5);
  AutoTest_Register("ui_wardrobe_system", AutoTest_UIWardrobeStep, 5);
  AutoTest_Register("cloth_secondary_motion", AutoTest_ClothSecondaryMotionStep, 130);
  AutoTest_Register("pve_pvp_balance_norm", AutoTest_PvEPvPBalanceStep, 5);
  AutoTest_Register("loadout_five_slots", AutoTest_LoadoutFiveSlotsStep, 5);
  AutoTest_Register("datau_quest_chain", AutoTest_DaTauQuestStep, 5);
  AutoTest_Register("pvp_dual_boss_escort", AutoTest_DualBossEscortStep, 5);
  AutoTest_Register("skill_use_xp_and_pvp_nullification", AutoTest_SkillXpAndPvpNullificationStep, 5);
  AutoTest_Register("desert_open_world_loop", AutoTest_DesertOpenWorldLoopStep, 5);
  AutoTest_Register("datau_npc_cycle_and_guiding_wisp", AutoTest_DaTauNpcCycleAndWispStep, 5);
  AutoTest_Register("net_p2p_explore_optimization", AutoTest_NetP2PExploreOptStep, 5);
  AutoTest_Register("ow_desert_sandbox_and_weather", AutoTest_DesertSandboxAndWeatherStep, 5);
  AutoTest_Register("spatial_hash_equivalence", AutoTest_SpatialHashEquivalenceStep, 5);
  AutoTest_Register("horde_swarm_ai_stress", AutoTest_HordeSwarmAIStressStep, 5);
  AutoTest_Register("dem_dual_scale_tiled", AutoTest_DemDualScaleTiledStep, 5);
}

