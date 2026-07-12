#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/metaball_fx.h"
#include "core/particle_system.h"
#include "compute/gpu_particle_system.h"
#include "core/post_fx.h"
#include "sandbox/sandbox_core.h"
#include "core/screen_distort.h"
#include "sandbox/skill_debugger.h"
#include "core/skill_manager.h"
#include "core/trail_system.h"
#include "sandbox/ui_panel.h"
#include "core/vfx_light.h"
#include "sandbox/vfx_test.h" // MỚI: Chỉ giữ duy nhất file test này để điều phối
#include "core/resource_manager.h"
#include "core/skill_helper.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/tuning.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "environment/environment_system.h"
#include "core/map_manager.h"
#include "skills/taiji/core_test/core_test_skill.h"
#include "sandbox/auto_test.h"
#include "sandbox/visual_verify.h"
#include "sandbox/pool_stats.h"
#include "core/status_vfx.h"
#include "core/afterimage.h"
#include "game/game_screen.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "skills/taiji/taiji_phong/taiji_phong_skill.h"
#include "game/game_rules.h"
#include "ai/ai.h"
#include <stdio.h>

// Biến camera toàn cục
Camera3D camera = {0};
PlayerEntity player = {0};

static void MyBeginMode3D(Camera3D camera) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPushMatrix();
  rlLoadIdentity();
  float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();

  if (camera.projection == CAMERA_PERSPECTIVE) {
    // near/far real-world-scaled (root CLAUDE.md "Standard coordinates &
    // scale") — NOT a straight ÷100 of the old 10.0/15000.0. Empirically,
    // near values below ~1.0 render a fully blank scene in this project's
    // rlFrustum() setup (bisected via autotest screenshots; root cause not
    // identified — suspected precision issue at very small frustum extents,
    // not a near/far *ratio* problem since the same ratio at 0.1/150 also
    // failed). sandbox_core.c's g_camDist is clamped with margin above this
    // near plane — keep core/screen_distort.c's SOFT_PARTICLE_SCENE_NEAR/FAR
    // in sync if this ever changes.
    double top = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    rlFrustum(-right, right, -top, top, 1.0, 1000.0);
  } else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
    double top = camera.fovy / 2.0;
    double right = top * aspect;
    rlOrtho(-right, right, -top, top, 0.0001, 150.0);
  }

  rlMatrixMode(RL_MODELVIEW);
  rlPushMatrix();
  rlLoadIdentity();
  Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
  rlMultMatrixf(MatrixToFloat(matView));
  rlEnableDepthTest();
}

static void MyEndMode3D(void) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPopMatrix();
  rlMatrixMode(RL_MODELVIEW);
  rlPopMatrix();
  rlLoadIdentity();
  rlDisableDepthTest();
}

// Smoke test for the autotest harness itself (sandbox/auto_test.h) — proves
// the whole pipeline (env var -> headless window -> fixed-dt frames ->
// registration -> step -> log -> summary -> exit code) works end to end.
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

  GameScreen_Init(&player);
  bool ok = AutoTest_ExpectTrue(GameScreen_GetState() == GAME_ARENA_INTRO, "match starts in intro", outReason, outReasonSize);

  // 2s intro at fixed dt → boss spawns, fight begins.
  for (int i = 0; i < 130 && GameScreen_GetState() == GAME_ARENA_INTRO; i++) {
    GameScreen_Update(&player, &cam, 1.0f / 60.0f);
  }
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_FIGHTING, "intro leads to fighting", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(Boss_IsAlive(), "boss spawned by intro", outReason, outReasonSize);

  // Boss death → victory.
  Entity_ApplyDamage(Boss_GetAgentId(), 1e9f, (Vector3){ 0 });
  Boss_Update(0.0f);
  GameScreen_Update(&player, &cam, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_VICTORY, "boss death gives victory", outReason, outReasonSize);

  // Fresh match, player death → defeat.
  GameScreen_Init(&player);
  for (int i = 0; i < 130 && GameScreen_GetState() == GAME_ARENA_INTRO; i++) {
    GameScreen_Update(&player, &cam, 1.0f / 60.0f);
  }
  Entity_ApplyDamage(player.agentId, 1e9f, (Vector3){ 0 });
  GameScreen_Update(&player, &cam, 1.0f / 60.0f);
  ok = ok && AutoTest_ExpectTrue(GameScreen_GetState() == GAME_DEFEAT, "player death gives defeat", outReason, outReasonSize);

  // Zone rule table (the one place gameplay rules live).
  ok = ok && AutoTest_ExpectFloatNear(GameRules_CooldownMult(0, NAT_RIVER), 0.5f, 0.001f, "thuy in river halves cooldown", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(GameRules_DamageMult(2, NAT_RIVER), 0.5f, 0.001f, "hoa in river halves damage", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectTrue(GameRules_GrantsStealth(1, NAT_FOREST), "moc in forest stealths", outReason, outReasonSize);
  ok = ok && AutoTest_ExpectFloatNear(GameRules_CooldownMult(2, NAT_RIVER), 1.0f, 0.001f, "no rule leaves 1.0", outReason, outReasonSize);

  // Reset for a clean pool (respawns the player agent, clears the boss).
  GameScreen_Init(&player);
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

// Step 0 (post-Module-7 hardening): real skills feed the combat registry.
// Casts a real FIRE dragon at an enemy agent and waits for the combat-
// applied hit (skills no longer damage projectiles themselves), and checks
// the match loop equips the default loadout.
static int s_step0Ally = -1, s_step0Foe = -1;
static AutoTestResult AutoTest_SkillRegistryStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    GameScreen_Init(&player); // also applies the default loadout
    const Agent *pa = Entity_GetAgent(player.agentId);
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
    GameScreen_Init(&player);
    return AUTOTEST_PASS;
  }
  return AUTOTEST_RUNNING; // maxFrames timeout fails the case
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

int main(int argc, char **argv) {
  // Widened/heightened from 1200x700 so the sandbox tuning panel
  // (sandbox/ui_panel.c) has room for multi-column tunable layouts as skills
  // gain more sandbox-tunable parameters (CORE_ISSUES.md Item 34 follow-up).
  const int screenWidth = 1280;
  const int screenHeight = 720;

  // --render-vfx <index> [--warmup <frames>] [--out <path>]
  // Renders NEWFX tab entry <index> headlessly, saves PNG, exits.
  bool        renderVFXMode   = false;
  int         renderVFXIndex  = 0;
  int         renderVFXWarmup = 90;
  const char *renderVFXOut    = "autotest_output/vfx_eval.png";
  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--render-vfx") == 0 && i + 1 < argc)
          { renderVFXIndex = atoi(argv[++i]); renderVFXMode = true; }
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
          renderVFXWarmup = atoi(argv[++i]);
      else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
          renderVFXOut = argv[++i];
  }

  bool autoTestMode     = AutoTest_IsEnabled();
  bool visualVerifyMode = VisualVerify_IsEnabled();
  bool headlessMode     = autoTestMode || visualVerifyMode || renderVFXMode;

  unsigned int configFlags = 0;
  if (headlessMode) {
      // Off-screen SetWindowPosition was tried first, but produced the exact
      // same GetWorldToScreen() output as FLAG_WINDOW_HIDDEN below (proving
      // the odd coordinates aren't a window-position artifact) — kept
      // FLAG_WINDOW_HIDDEN since it doesn't touch window placement at all,
      // closer to normal interactive behavior.
      configFlags |= FLAG_WINDOW_HIDDEN;
  }
  SetConfigFlags(configFlags);
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");

  rlSetClipPlanes(0.001f, 150.0f);

  // Tự động sinh các texture cơ bản nếu thiếu trong thư mục assets/textures
  if (!FileExists("assets/textures/noise.png")) {
      Image noiseImg = GenImagePerlinNoise(256, 256, 0, 0, 16.0f);
      ExportImage(noiseImg, "assets/textures/noise.png");
      UnloadImage(noiseImg);
  }
  if (!FileExists("assets/textures/flare.png")) {
      Image flareImg = GenImageGradientRadial(128, 128, 0.0f, WHITE, BLANK);
      ExportImage(flareImg, "assets/textures/flare.png");
      UnloadImage(flareImg);
  }
  if (!FileExists("assets/textures/crack.png")) {
      Image crackImg = GenImageCellular(256, 256, 32);
      ExportImage(crackImg, "assets/textures/crack.png");
      UnloadImage(crackImg);
  }
  if (!FileExists("assets/textures/water_caustics.png")) {
      Image causticsImg = GenImageCellular(256, 256, 16);
      ExportImage(causticsImg, "assets/textures/water_caustics.png");
      UnloadImage(causticsImg);
  }

  // -----------------------------------------------------------------
  // KHỞI TẠO CÁC HỆ THỐNG ĐỒ HỌA VFX
  // -----------------------------------------------------------------
  InitParticleSystem();
  GpuParticleSystem_Init();
  Shader defaultTrailShader = LoadShader(0, "core/shaders/trail_glow.fs");
  InitTrailSystem(defaultTrailShader);
  VFXLight_Init();
  DecalSystem_Init();
  ScreenDistort_Init(screenWidth, screenHeight);
  PostFX_Init(screenWidth, screenHeight);
  MetaballFX_Init(screenWidth, screenHeight);

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  Image trailImg = {
      .data = MemAlloc(64 * sizeof(Color)),
      .width = 64,
      .height = 1,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
  };
  Color *trailPixels = (Color *)trailImg.data;
  for(int i=0; i<64; i++) {
      float u = i / 63.0f;
      float dist = fabsf(u - 0.5f) * 2.0f;
      float alpha = 1.0f - dist;
      trailPixels[i] = (Color){255, 255, 255, (unsigned char)(255 * alpha)};
  }
  Texture2D globalTrailTex = LoadTextureFromImage(trailImg);
  UnloadImage(trailImg);
  TrailSystem_SetGlobalTexture(globalTrailTex);
  UnloadImage(img);

  Image atlasImg = GenImageColor(128, 128, BLANK);
  ImageDrawCircle(&atlasImg, 32, 32, 20, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 12, 12, 40, 40, WHITE);
  ImageDrawCircle(&atlasImg, 32, 96, 12, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 16, 64 + 16, 32, 32, WHITE);
  Texture2D testAtlasTex = LoadTextureFromImage(atlasImg);
  UnloadImage(atlasImg);

  ResourceManager_Init();
  Tuning_Init("tuning.cfg");
  InitSkillManager(screenWidth, screenHeight);
  if (autoTestMode) {
      AutoTest_Register("smoke_skill_manager_init", AutoTest_SmokeStep, 5);
      AutoTest_Register("entities_combat_v2", AutoTest_EntitiesCombatV2Step, 5);
      AutoTest_Register("map_trigger_zones", AutoTest_MapZonesStep, 5);
      AutoTest_Register("combat_clash_matrix", AutoTest_CombatClashStep, 5);
      AutoTest_Register("control_intent", AutoTest_ControlStep, 5);
      AutoTest_Register("boss_hac_dien", AutoTest_BossStep, 5);
      AutoTest_Register("taiji_state", AutoTest_TaijiStep, 5);
      AutoTest_Register("game_mode_loop", AutoTest_GameModeStep, 5);
      AutoTest_Register("skill_combat_registry", AutoTest_SkillRegistryStep, 300);
  }
  DamageVolume_Init();
  EmitterSystem_Init();
  Afterimage_Init();
  PoolStats_Init();
  RegisterStaticOccluder((Vector3){4.0f, 0.0f, 3.2f}, 0.25f, 0.625f);
  RegisterStaticOccluder((Vector3){8.0f, 0.0f, 5.2f}, 0.3f, 0.75f);
  RegisterStaticOccluder((Vector3){6.0f, 0.0f, 2.6f}, 0.2f, 0.5f);
  InitUIPanel();
  SkillDebugger_Init();
  Environment_Init();
  MapManager_Init();
  Combat_Init();

  EnemyEntity enemy;
  InitSandbox(&player, &enemy);
  GameScreen_Init(&player);

  UIPanelState uiState = {0};
  uiState.activeSkillIndex = 0;
  uiState.currentParams.level = 1;
  uiState.currentParams.milestone = 1;
  uiState.currentParams.sizeScale = 1.0f;
  uiState.currentParams.quantity = 3;
  uiState.currentParams.anchorType = CAST_ANCHOR_TARGET;
  uiState.currentParams.pathType = CAST_PATH_PROJECTILE;
  uiState.currentParams.showPortal = true;
  uiState.currentParams.damage = 100.0f;
  uiState.isPanelOpen = false;

  PostFXConfig postFXConfig = {.bloomEnabled = true,
                               .bloomThreshold = 0.5f,
                               .bloomIntensity = 2.0f,
                               .chromaticEnabled = true,
                               .chromaticStrength = 0.15f,
                               .vignetteEnabled = true,
                               .vignetteRadius = 0.85f,
                               .vignetteSoftness = 0.45f,
                               .colorGradeEnabled = true,
                               .contrast = 1.05f,
                               .saturation = 1.15f,
                               .colorTint = {1.0f, 1.0f, 1.0f}};

  if (visualVerifyMode) {
      VisualVerify_Init(Skill_GetIndexByName(VisualVerify_GetSkillName()));
  }

  if (!autoTestMode && !visualVerifyMode) SetTargetFPS(60);

  bool g_gamePaused = false;
  bool g_stepNextFrame = false;
  bool g_slowMotion = false;

  float g_totalElapsed = 0.0f;

  typedef enum {
      SCREEN_MAIN_MENU,
      SCREEN_SKILL_SANDBOX,
      SCREEN_VFX_TESTER,
      SCREEN_GAME
  } GameScreen;
  GameScreen currentScreen = SCREEN_MAIN_MENU;
  // Headless modes never click through the menu — drop straight into the
  // sandbox screen so AutoTest_RunFrame/VisualVerify actually tick (the menu
  // branch `continue;`s past them, which used to hang autotest forever).
  if (autoTestMode || visualVerifyMode) currentScreen = SCREEN_SKILL_SANDBOX;
  int renderVFXFrame = 0;
  if (renderVFXMode) {
      currentScreen    = SCREEN_VFX_TESTER;
      player.position  = (Vector3){6.0f, 0.0f, 4.4f}; // arena center
      VFXTest_SetRenderTarget(renderVFXIndex, player.position);
  }
  while (autoTestMode     ? !AutoTest_IsFinished()      :
         visualVerifyMode ? !VisualVerify_IsFinished()  :
         renderVFXMode    ? (renderVFXFrame <= renderVFXWarmup) :
         !WindowShouldClose()) {
    float dt = (autoTestMode || visualVerifyMode || renderVFXMode) ? (1.0f / 60.0f) : TimeFX_Apply(GetFrameTime());
    g_totalElapsed += dt;

    // -------------------------------------------------------------------------
    // TIME CONTROL FOR DEBUGGING / SCREENSHOTTING
    // -------------------------------------------------------------------------
    if (IsKeyPressed(KEY_V)) g_gamePaused = !g_gamePaused;
    if (IsKeyPressed(KEY_B)) g_stepNextFrame = true;
    if (IsKeyPressed(KEY_M)) g_slowMotion = !g_slowMotion;
    if (IsKeyPressed(KEY_K)) {
        int nextMap = (MapManager_GetActiveIndex() + 1) % MapManager_GetCount();
        MapManager_SetActiveIndex(nextMap);
    }

    if (g_gamePaused) {
        if (g_stepNextFrame) {
            dt = 1.0f / 60.0f; // Force exactly 1 frame of time
            g_stepNextFrame = false;
        } else {
            dt = 0.0f;
        }
    } else if (g_slowMotion) {
        dt *= 0.1f; // Slow motion 10% speed
    }
    
    SkillDebugger_CheckInput();
    if (g_isDebuggerCapturing) {
        dt = 0.0f; // Freeze time during automated screenshot capture
    }

    // Combat event frame boundary: last frame's clash events stay peekable
    // through this frame's skill updates, then get cleared here.
    Combat_BeginFrame();

    if (currentScreen == SCREEN_MAIN_MENU) {
        Vector2 mousePos = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        Rectangle btnSandbox = { sw/2 - 150, sh/2 - 60, 300, 50 };
        Rectangle btnVFX = { sw/2 - 150, sh/2 + 20, 300, 50 };
        Rectangle btnGame = { sw/2 - 150, sh/2 + 100, 300, 50 };

        if (CheckCollisionPointRec(mousePos, btnSandbox) && clicked) {
            currentScreen = SCREEN_SKILL_SANDBOX;
        }
        if (CheckCollisionPointRec(mousePos, btnVFX) && clicked) {
            currentScreen = SCREEN_VFX_TESTER;
        }
        if (CheckCollisionPointRec(mousePos, btnGame) && clicked) {
            currentScreen = SCREEN_GAME;
        }

        BeginDrawing();
        ClearBackground(DARKGRAY);

        const char* title = "WUXING SKILLS TESTBED";
        int titleW = MeasureText(title, 30);
        DrawText(title, sw/2 - titleW/2, sh/2 - 150, 30, WHITE);

        DrawRectangleRounded(btnSandbox, 0.2f, 10, CheckCollisionPointRec(mousePos, btnSandbox) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnSandbox, 0.2f, 10, WHITE);
        DrawText("1. ENTER SKILL SANDBOX", (int)btnSandbox.x + 30, (int)btnSandbox.y + 15, 20, BLACK);

        DrawRectangleRounded(btnVFX, 0.2f, 10, CheckCollisionPointRec(mousePos, btnVFX) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnVFX, 0.2f, 10, WHITE);
        DrawText("2. ENTER VFX PREFAB TESTER", (int)btnVFX.x + 10, (int)btnVFX.y + 15, 20, BLACK);

        DrawRectangleRounded(btnGame, 0.2f, 10, CheckCollisionPointRec(mousePos, btnGame) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnGame, 0.2f, 10, WHITE);
        DrawText("3. ENTER GAME", (int)btnGame.x + 90, (int)btnGame.y + 15, 20, BLACK);

        EndDrawing();
        continue;
    }

    Vector3 mouseTarget3D = {0};

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        UpdateUIPanel(GetMousePosition(), &uiState);
        if (uiState.requestedBackToMenu) currentScreen = SCREEN_MAIN_MENU;

        UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);
        CameraFX_Update(&camera, dt);
    } else if (currentScreen == SCREEN_VFX_TESTER) {
        static float vfxCameraAngle = 0.0f;
        float speed = 20.0f;
        float s = sinf(vfxCameraAngle);
        float c = cosf(vfxCameraAngle);

        if (IsKeyDown(KEY_W)) { player.position.x -= s * speed * dt; player.position.z -= c * speed * dt; }
        if (IsKeyDown(KEY_S)) { player.position.x += s * speed * dt; player.position.z += c * speed * dt; }
        if (IsKeyDown(KEY_A)) { player.position.x -= c * speed * dt; player.position.z += s * speed * dt; }
        if (IsKeyDown(KEY_D)) { player.position.x += c * speed * dt; player.position.z -= s * speed * dt; }

        if (IsKeyDown(KEY_Q)) vfxCameraAngle -= 2.5f * dt;
        if (IsKeyDown(KEY_E)) vfxCameraAngle += 2.5f * dt;

        static float vfxCamDist = 8.4f;
        vfxCamDist -= GetMouseWheelMove() * 0.5f;
        if (vfxCamDist < 2.0f) vfxCamDist = 2.0f;
        if (vfxCamDist > 30.0f) vfxCamDist = 30.0f;

        camera.target = (Vector3){ player.position.x, player.position.y + 0.2f, player.position.z };
        camera.position = (Vector3){ 
            player.position.x + sinf(vfxCameraAngle) * vfxCamDist, 
            player.position.y + vfxCamDist * 0.8f, 
            player.position.z + cosf(vfxCameraAngle) * vfxCamDist
        };

        // Intersect against the flat Y=0 plane first (cheap, works for the
        // common flat-map case), then snap the result's Y to the ACTIVE
        // map's real ground height — on a heightmap map (e.g. VERDANT_PATH)
        // Y=0 is only correct on the plateau interior; anywhere else (near
        // the cliff falloff) this used to leave mouseTarget3D.y wrong,
        // which broke ground-hugging/multi-point NEWFX effects that assume
        // it's the real surface (e.g. FISSURE's start point).
        Ray mouseRay = GetScreenToWorldRay(GetMousePosition(), camera);
        float t = -mouseRay.position.y / mouseRay.direction.y;
        float mtX = mouseRay.position.x + mouseRay.direction.x * t;
        float mtZ = mouseRay.position.z + mouseRay.direction.z * t;
        mouseTarget3D = (Vector3){ mtX, MapManager_GetGroundHeightAt(mtX, mtZ), mtZ };

        if (VFXTest_UpdateAndHandleInput(player.position, mouseTarget3D, testAtlasTex, globalParticleTex)) {
            currentScreen = SCREEN_MAIN_MENU;
        }
        CameraFX_Update(&camera, dt);
    } else if (currentScreen == SCREEN_GAME) {
        GameScreen_Update(&player, &camera, dt);
        if (GameScreen_RequestedBackToMenu()) currentScreen = SCREEN_MAIN_MENU;
        CameraFX_Update(&camera, dt);
    }

    // Boss AI then Đấu Pháp resolve — boss casts submit through skills into
    // the combat registry, so Boss_Update runs first; Combat_Update last,
    // after all skill updates submitted this frame's projectile colliders
    // (immediate mode). Both tick for every screen; no boss / no
    // submissions = no-op. (Module 7 game/ will own this ordering.)
    AI_Update(dt);
    Boss_Update(dt);
    Combat_Update(dt);

    // Minion self-destruct VFX — ai/ is pure logic and reports explosions
    // as events; the composition layer draws them (element-matched preset).
    {
        MinionExplosion booms[8];
        int nBooms = AI_PollExplosions(booms, 8);
        for (int bi = 0; bi < nBooms; bi++) {
            EffectPresetType preset =
                (booms[bi].element == 0) ? EFFECT_PRESET_WATER_SPLASH :
                (booms[bi].element == 1) ? EFFECT_PRESET_WOOD_BLOOM :
                (booms[bi].element == 2) ? EFFECT_PRESET_FIRE_EXPLOSION :
                (booms[bi].element == 3) ? EFFECT_PRESET_EARTH_CRACK :
                                           EFFECT_PRESET_METAL_SHARD;
            VFX_ComposeImpact(booms[bi].pos, preset, 0.8f);
        }
    }

    // Cảnh Giới Thái Cực → monochrome world (Module 6). Any live taiji
    // agent (player via balanced loadout, boss below 30% HP) fades the
    // whole canvas to black-and-white; fades back out on exit.
    {
        static float s_taijiMono = 0.0f;
        bool anyTaiji = Entity_IsTaijiActive(Control_GetAgentId()) ||
                        (Boss_IsAlive() && Entity_IsTaijiActive(Boss_GetAgentId()));
        float target = anyTaiji ? 1.0f : 0.0f;
        float speed = 2.5f * dt; // ~0.4s fade
        if (s_taijiMono < target)      { s_taijiMono += speed; if (s_taijiMono > target) s_taijiMono = target; }
        else if (s_taijiMono > target) { s_taijiMono -= speed; if (s_taijiMono < target) s_taijiMono = target; }
        PostFX_SetMonochrome(s_taijiMono);
    }

    static bool isDragging = false;
    static int pathCount = 0;
    static Vector3 pathPoints[32];

    if (currentScreen == SCREEN_SKILL_SANDBOX && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      isDragging = true;
      pathCount = 1;
      pathPoints[0] = mouseTarget3D;
    }

    if (isDragging) {
      // Add points if distance > 5.0f
      if (pathCount < 32) {
        if (Vector3Distance(mouseTarget3D, pathPoints[pathCount - 1]) > 5.0f) {
          pathPoints[pathCount++] = mouseTarget3D;
        }
      }

      // Draw the drag path for visual feedback
      for (int i = 0; i < pathCount - 1; i++) {
        DrawLine3D(pathPoints[i], pathPoints[i + 1], GREEN);
      }

      if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        isDragging = false;
        uiState.currentParams.pathPointCount = pathCount;
        for (int i = 0; i < pathCount; i++) {
          uiState.currentParams.pathPoints[i] = pathPoints[i];
        }
        // Cast anim gated on the bool result — no flourish when the mana
        // gate (or bounds check) rejected the cast. Flourish length is the
        // skill's own registered duration, not a fixed number.
        if (CastSkill(uiState.activeSkillIndex, player.agentId, player.position,
                      mouseTarget3D, uiState.currentParams)) {
          CharacterModel_TriggerAttackTimed(&player.anim, CHAR_ANIM_CAST,
                                            Skill_GetCastAnimSeconds(uiState.activeSkillIndex));
          Sandbox_FacePlayerToward(&player, mouseTarget3D); // quay về hướng đánh
        }
      }
    }

    Tuning_Update();
    UpdateSkillManager(dt, enemy.position, 0.35f);
    DamageVolume_Update(dt);
    VFX_Compose_Update(dt);
    EmitterSystem_Update(dt);
    StatusVFX_Update(dt);
    Afterimage_Update(dt);
    UpdateParticles(dt);
    GpuParticleSystem_Update(dt);
    UpdateTrailSystem(dt);
    VFXLight_Update(dt);
    DecalSystem_Update(dt);
    ScreenDistort_Update(dt);
    Environment_Update(dt);
    MapManager_Update(dt);

    SkillDebugger_PreRender();

    BeginDrawing();

    ScreenDistort_Begin();
    if (g_isDebuggerCapturing) {
        ClearBackground(BLACK);
    } else {
        ClearBackground(GetColor(0x111111FF));
    }

    MyBeginMode3D(camera);
    MapManager_DrawActive();
    if (!g_isDebuggerCapturing && currentScreen == SCREEN_SKILL_SANDBOX) {
        DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    }

    // Vẽ Decal hệ thống sát sàn đấu
    if (!g_debugHideDecals) {
        DecalSystem_SetCamera(camera);
        DecalSystem_Draw();
    }

    if (!g_debugHideMeshes) {
        DrawSkillManagerWorld3D();
    }

    // =========================================================================
    // MỚI: TOÀN BỘ PHẦN TRUY XUẤT VÀ VẼ KHỐI CẦU DEBUG LIGHT ĐÃ ĐƯỢC BỐC SANG ĐÂY
    // + ĐƯỢC BỔ SUNG THÊM VIỆC VẼ MESH TỪ PREFAB TESTER
    // =========================================================================
    if (currentScreen == SCREEN_VFX_TESTER) {
        VFXTest_Draw3D();

        if (!renderVFXMode) {
            Environment_DrawSmartShadow(player.position, ENV_SHAPE_SPHERE, 0.25f, 0.25f);
            DrawCharacter3D(player.position, 0.25f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF), true, mouseTarget3D);
        }

        // Stateful archetypes (VFX_SpawnProcBeam/Orbitals, VFX_ComposeLightningBolt's
        // ProcBolt handle) live in a global pool updated unconditionally every frame
        // (VFX_Compose_Update above) but were only ever DRAWN in SCREEN_SKILL_SANDBOX —
        // triggering their NEWFX tab entries (BOLT/PROC BEAM/ORBITALS) spawned them
        // correctly into the pool but nothing rendered. Draw here too.
        VFX_Compose_Draw3D(camera);
    }

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        VFX_Compose_Draw3D(camera);
    }
    if (currentScreen == SCREEN_GAME) {
        GameScreen_Draw3D(&player);
        Boss_Draw();
    }
    Afterimage_Draw();

    if (!g_debugHideTrails) {
        DrawTrailEntities(camera);
    }

    if (!g_debugHideParticles) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawParticles(camera, globalParticleTex);
        GpuParticleSystem_Draw(camera, globalParticleTex);
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    MyEndMode3D();
    ScreenDistort_End();
    ScreenDistort_SnapshotDepth(); // soft particles: snapshot this frame's depth for next frame's sampling

    PostFX_Begin();
    ClearBackground(BLACK);
    ScreenDistort_Draw(camera);
    PostFX_End();

    ClearBackground(BLACK);
    PostFX_Draw(&postFXConfig);

    // Metaballs: composite directly onto the screen, after post-process —
    // must run outside PostFX_Begin/End (BeginTextureMode can't nest) and
    // after PostFX_Draw (which would otherwise overwrite it).
    MetaballFX_DrawRegistered(camera, ELEMENT_COLOR_WATER, 0.3f, 0.12f);

    // These are dev/debug overlays — skip them entirely on SCREEN_GAME so it
    // reads as a real production screen, not a test environment. Untouched
    // for every other screen.
    if (currentScreen != SCREEN_GAME) {
        DrawSkillManagerOverlay();
        DrawCoreTestSkillDebugHUD();
        PoolStats_DrawOverlay(); // CORE_ISSUES.md Item 3 test — on-screen depth readback (press L)
    }

    if (!renderVFXMode && currentScreen != SCREEN_GAME) {
        Vector2 enemyScreenHead = GetWorldToScreen(
            (Vector3){enemy.position.x, enemy.position.y + 0.55f, enemy.position.z},
            camera);
        DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
                 WHITE);
    }

    if (!g_isDebuggerCapturing && !renderVFXMode) {
        if (currentScreen == SCREEN_SKILL_SANDBOX) {
            DrawUIPanel(&uiState);
            DrawSandboxTouchControls(&player);
            if (uiState.isPanelOpen) {
                DrawSandboxHUD();
            }
        } else if (currentScreen == SCREEN_VFX_TESTER && !renderVFXMode) {
            VFXTest_DrawHUD();
        } else if (currentScreen == SCREEN_GAME) {
            GameScreen_DrawHUD(&player);
        }

        if (currentScreen != SCREEN_GAME) {
            DrawText(TextFormat("FPS: %d", GetFPS()), 10, 640, 20, GREEN);
        }
    }
             
    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        SkillDebugger_PostRender(uiState.activeSkillIndex, player.position, mouseTarget3D);
    }

    EndDrawing();

    if (autoTestMode)     AutoTest_RunFrame();
    if (visualVerifyMode) VisualVerify_RunFrame(g_totalElapsed);
    if (renderVFXMode) {
        renderVFXFrame++;
        if (renderVFXFrame >= renderVFXWarmup) {
            if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
            Image img = LoadImageFromScreen();
            ExportImage(img, renderVFXOut);
            UnloadImage(img);
            break;
        }
    }
  }

  int exitCode = 0;
  if (autoTestMode) {
      AutoTest_PrintSummary();
      exitCode = AutoTest_GetExitCode();
  }
  if (visualVerifyMode) {
      exitCode = VisualVerify_GetExitCode();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  UnloadParticleSystem();
  GpuParticleSystem_Unload();
  UnloadTrailSystem();
  DecalSystem_Unload();
  ScreenDistort_Unload();
  MetaballFX_Unload();
  UnloadSkillManager();
  DamageVolume_Unload();
  EmitterSystem_Unload();
  ResourceManager_Unload();
  MapManager_Unload();
  CloseWindow();

  return exitCode;
}