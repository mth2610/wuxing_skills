#include "core/camera_fx.h"
#include "core/audio_system.h"
#include "core/decal_system.h"
#include "core/metaball_fx.h"
#include "core/particle_system.h"
#include "compute/gpu_particle_system.h"
#include "core/post_fx.h"
#include "sandbox/sandbox_core.h"
#include "core/screen_distort.h"
#include "core/surface_material.h"
#include "core/gfx_quality.h"
#include "core/atmosphere.h"
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
#include "environment/env_shadow.h"
#include "maps/toolkit/ground_shadow.h"
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
#include "ui/ui.h"
#include "net/net_transport.h"
#include "formations/formation_system.h"
#include "net/net.h"
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

static void AutoTest_ResetArenaToDefault(void); // defined below (needs `player`)

// Step 0 (post-Module-7 hardening): real skills feed the combat registry.
// Casts a real FIRE dragon at an enemy agent and waits for the combat-
// applied hit (skills no longer damage projectiles themselves), and checks
// the match loop equips the default loadout.
static int s_step0Ally = -1, s_step0Foe = -1;
static AutoTestResult AutoTest_SkillRegistryStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    GameScreen_Init(&player); // also applies the default loadout
    AutoTest_ResetArenaToDefault();
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
  player.position = (Vector3){ -11.0f, 0.0f, 4.4f }; // sandbox home
  Entity_SetPosition(player.agentId, player.position);
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
    ok = ok && AutoTest_ExpectTrue(ba->mana < ba->maxMana - 0.01f,
                                   "bot cast at least once (mana spent)", outReason, outReasonSize);
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

int main(int argc, char **argv) {
  // Widened/heightened from 1200x700 so the sandbox tuning panel
  // (sandbox/ui_panel.c) has room for multi-column tunable layouts as skills
  // gain more sandbox-tunable parameters (CORE_ISSUES.md Item 34 follow-up).
  // Non-const: on Android these are reassigned to the native display size after
  // InitWindow so raylib does NOT upscale (render size == display size). A
  // render≠display mismatch makes raylib blit through an offscreen target that
  // renders black on some devices (Mali), so the whole app (even 2D menu) shows
  // black. Everything downstream (RTs, camera, UI) reads these, so it adapts.
  int screenWidth = 1280;
  int screenHeight = 720;

  // --render-vfx <index> [--warmup <frames>] [--out <path>]
  // Renders NEWFX tab entry <index> headlessly, saves PNG, exits.
  bool        renderVFXMode   = false;
  int         renderVFXIndex  = 0;
  int         renderVFXWarmup = 90;
  const char *renderVFXOut    = "autotest_output/vfx_eval.png";
  // WUXING_SHADOW_TEST=1: headless real-shadow-map verify (REAL_SHADING_P6_NOTES.md
  // session-4 part 6 — deterministic replacement for manual in-game screenshot
  // round-trips, which kept losing sync because the player moved between builds).
  // Fixes player at the arena center, enables EnvShadow, waits WUXING_SHADOW_TEST_WARMUP
  // frames (default matches renderVFXWarmup), saves a screenshot + one TraceLog
  // pixel-readback line, exits.
  bool        shadowTestMode   = (getenv("WUXING_SHADOW_TEST") != NULL);
  int         shadowTestWarmup = getenv("WUXING_SHADOW_TEST_WARMUP") ? atoi(getenv("WUXING_SHADOW_TEST_WARMUP")) : 20;
  int         netHostPort     = 0;      // --host [port]
  const char *netJoinIp       = NULL;   // --join <ip> [port]
  int         netJoinPort     = NET_DEFAULT_PORT;
  bool        netHostOnline   = false;  // --host-online (EOS, prints join code)
  const char *netJoinCode     = NULL;   // --join-online <code>
  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--host") == 0)
          netHostPort = (i + 1 < argc && argv[i+1][0] != '-') ? atoi(argv[++i]) : NET_DEFAULT_PORT;
      else if (strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
          netJoinIp = argv[++i];
          if (i + 1 < argc && argv[i+1][0] != '-') netJoinPort = atoi(argv[++i]);
      }
      else if (strcmp(argv[i], "--host-online") == 0)
          netHostOnline = true;
      else if (strcmp(argv[i], "--join-online") == 0 && i + 1 < argc)
          netJoinCode = argv[++i];
      else if (strcmp(argv[i], "--render-vfx") == 0 && i + 1 < argc)
          { renderVFXIndex = atoi(argv[++i]); renderVFXMode = true; }
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
          renderVFXWarmup = atoi(argv[++i]);
      else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
          renderVFXOut = argv[++i];
  }

  bool autoTestMode     = AutoTest_IsEnabled();
  bool visualVerifyMode = VisualVerify_IsEnabled();
  bool headlessMode     = autoTestMode || visualVerifyMode || renderVFXMode || shadowTestMode;

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

  // Audio device + SFX/music framework (no-op & silent until the user drops
  // assets under assets/audio/). Headless autotest/render modes skip it.
  if (!headlessMode) Audio_Init();

  rlSetClipPlanes(0.001f, 150.0f);

#ifndef PLATFORM_ANDROID
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
#endif

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
  SurfaceMaterial_Init(); // G2 — must precede InitSandbox (CharacterModel_Load applies it)
  GfxQuality_Set(GfxQuality_Default()); // Real Shading P0 — platform-appropriate tier
  Atmosphere_Init();      // G3 — ambient dust motes over the arena
  Atmosphere_Configure((Vector3){6.0f, 3.0f, 4.4f}, (Vector3){15.0f, 5.0f, 15.0f},
                       340, (Color){160, 190, 235, 255});
  MetaballFX_Init(screenWidth, screenHeight);
  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK);
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  // BILINEAR bắt buộc: mặc định raylib là POINT (GL_NEAREST) — hạt billboard
  // phóng to (vd Fire) stretch texel 64x64 thành khối vuông cứng lộ viền rõ
  // (thấy trên path CPU/VBO Android; path COMPUTE desktop ít lộ hơn do hạt
  // thường nhỏ hơn). Cùng quy ước với atmosphere/flow_map/metaball_fx/post_fx.
  SetTextureFilter(globalParticleTex, TEXTURE_FILTER_BILINEAR);
  Image trailImg = GenImageColor(64, 64, BLANK);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      float u = x / 63.0f;
      float dist = fabsf(u - 0.5f) * 2.0f;
      float alpha = fmaxf(0.0f, 1.0f - dist * dist);
      ImageDrawPixel(&trailImg, x, y, (Color){255, 255, 255, (unsigned char)(255 * alpha)});
    }
  }
  Texture2D globalTrailTex = LoadTextureFromImage(trailImg);
  UnloadImage(trailImg);
  SetTextureFilter(globalTrailTex, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(globalTrailTex, TEXTURE_WRAP_CLAMP);
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
      AutoTest_Register("minion_ai", AutoTest_MinionAIStep, 5);
      AutoTest_Register("ui_auto_target", AutoTest_AutoTargetStep, 5);
      AutoTest_Register("formation_tran_phap", AutoTest_FormationStep, 5);
      AutoTest_Register("net_wire_format", AutoTest_NetWireStep, 5);
      AutoTest_Register("team_elimination", AutoTest_TeamEliminationStep, 5);
      AutoTest_Register("hero_bot_handicap", AutoTest_HeroBotHandicapStep, 5);
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
  EnvShadow_Init(); // Real Shading P6 — depth-only shadow map, OFF until toggled
  MapManager_Init();
  Combat_Init();

  EnemyEntity enemy;
  InitSandbox(&player, &enemy);
  GameScreen_Init(&player);

  // --host / --join (ENet, LAN) or --host-online / --join-online (EOS,
  // internet): bring the endpoint up and drop into the LOBBY screen (Đợt
  // A2 — the room gathers there; the host's BAT DAU moves everyone into
  // the match together).
  bool netRequested = false;
  char roomCode[16] = { 0 }; // shown in the lobby + match HUD (host only)
  if (netHostPort > 0) netRequested = Net_StartHost(netHostPort);
  else if (netJoinIp != NULL) netRequested = Net_StartClient(netJoinIp, netJoinPort);
  else if (netHostOnline) {
      netRequested = Net_StartHostOnline(roomCode, (int)sizeof(roomCode));
      if (netRequested) {
          GameScreen_SetOnlineCode(roomCode); // HUD shows it while waiting

          // The one line the host reads to their friend — keep it loud.
          printf("\n==============================\n"
                 "  WUXING ONLINE — JOIN CODE: %s\n"
                 "  (ban be: ./wuxing --join-online %s)\n"
                 "==============================\n\n", roomCode, roomCode);
      }
  }
  else if (netJoinCode != NULL) netRequested = Net_JoinOnline(netJoinCode);
  if ((netHostPort > 0 || netJoinIp || netHostOnline || netJoinCode) && !netRequested) {
      TraceLog(LOG_WARNING, "[NET] failed to start %s",
               (netHostPort > 0 || netHostOnline) ? "host" : "client");
  }

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
                               .contrast = 1.08f,
                               .saturation = 1.28f, // ACES desaturates — lift richness back
                               .colorTint = {1.0f, 1.0f, 1.0f},
                               // Split-tone: cool moonlit shadows, warm highlights (Moonlight Blade mood).
                               .shadowTint = {0.90f, 0.97f, 1.12f},
                               .highlightTint = {1.10f, 1.02f, 0.90f},
                               // Đợt G1 — cinematic tone mapping on by default.
                               .tonemapEnabled = true,
                               .exposure = 1.12f};

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
      SCREEN_GAME,
      SCREEN_LOBBY   // net room (Đợt A2) — waits for the host's BAT DAU
  } GameScreen;
  GameScreen currentScreen = SCREEN_MAIN_MENU;
  if (netRequested) currentScreen = SCREEN_LOBBY; // PvP run: gather in the room

  // Main-menu online (EOS) UI state — TAO PHONG / NHAP MA buttons. Actions
  // are queued one frame (menuOnlinePending) so the "DANG KET NOI" overlay
  // renders before the blocking EOS setup call freezes the thread.
  char menuOnlineMsg[64] = "";
  char menuJoinInput[8]  = "";
  int  menuJoinLen       = 0;
  bool menuJoinOpen      = false;
  int  menuOnlinePending = 0; // 0 none, 1 host, 2 join
  // Headless modes never click through the menu — drop straight into the
  // sandbox screen so AutoTest_RunFrame/VisualVerify actually tick (the menu
  // branch `continue;`s past them, which used to hang autotest forever).
  if (autoTestMode || visualVerifyMode || shadowTestMode) currentScreen = SCREEN_SKILL_SANDBOX;
  // Dev: WUXING_MAP=<name substring> forces the active map (case-insensitive)
  // so headless verify/screenshot runs can target a specific world.
  {
    const char *wantMap = getenv("WUXING_MAP");
    if (wantMap != NULL && wantMap[0] != '\0') {
      for (int i = 0; i < MapManager_GetCount(); i++) {
        if (strcasestr(MapManager_GetName(i), wantMap) != NULL) {
          MapManager_SetActiveIndex(i);
          TraceLog(LOG_INFO, "WUXING_MAP: active map -> %s", MapManager_GetName(i));
          break;
        }
      }
    }
  }
  int renderVFXFrame = 0;
  if (renderVFXMode) {
      currentScreen    = SCREEN_VFX_TESTER;
      player.position  = (Vector3){6.0f, 0.0f, 4.4f}; // arena center
      VFXTest_SetRenderTarget(renderVFXIndex, player.position);
  }
  int shadowTestFrame = 0;
  if (shadowTestMode) {
      // Arena center — deterministic, always inside default_arena's ground
      // plate and inside EnvShadow's light frustum. WUXING_MAP can override
      // the map (see above); the position stays valid for default_arena and
      // verdant_path since both use the same MAP_API.md arena coordinates.
      player.position = (Vector3){6.0f, 0.0f, 4.4f};
      EnvShadow_SetEnabled(true);
      TraceLog(LOG_INFO, "WUXING_SHADOW_TEST: player @ arena center, shadow enabled, %d warmup frames", shadowTestWarmup);
  }
  while (autoTestMode     ? !AutoTest_IsFinished()      :
         visualVerifyMode ? !VisualVerify_IsFinished()  :
         renderVFXMode    ? (renderVFXFrame <= renderVFXWarmup) :
         shadowTestMode   ? (shadowTestFrame <= shadowTestWarmup) :
         !WindowShouldClose()) {
    float dt = (autoTestMode || visualVerifyMode || renderVFXMode || shadowTestMode) ? (1.0f / 60.0f) : TimeFX_Apply(GetFrameTime());
    g_totalElapsed += dt;

    // Android EGL present diagnostic (WUXING_PRESENT_TEST=1): bypasses ALL game
    // rendering and draws only a solid color + shapes. RESOLVED 14/07/2026:
    // the "black + uncapped fps" this test exposed was raylib built with
    // -DCUSTOMIZE_BUILD=ON (EndDrawing neither swapped nor paced — see
    // Makefile.Android compile_raylib_android + ANDROID_NOTICES.md §D2).
    // Kept as a cheap present-path sanity toggle.
    if (getenv("WUXING_PRESENT_TEST")) {
        BeginDrawing();
        ClearBackground(RED);
        DrawCircle(GetScreenWidth()/2, GetScreenHeight()/2, 200, WHITE);
        DrawText("PRESENT OK", 60, 60, 60, GREEN);
        EndDrawing();
        continue;
    }

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
    if (IsKeyPressed(KEY_L)) {
        GfxQuality_Set((GfxQuality)((GfxQuality_Get() + 1) % 4)); // Real Shading — cycle UNLIT..HIGH
    }
    if (IsKeyPressed(KEY_J)) {
        EnvShadow_SetEnabled(!EnvShadow_IsEnabled()); // Real Shading P6 — toggle real shadow map
    }
    if (IsKeyPressed(KEY_H) && EnvShadow_IsEnabled()) {
        EnvShadow_DebugDump(player.position); // P6 diag — numeric shadow-map readback (see notes)
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

    Audio_Update(dt); // streams the music bed (SFX are fire-and-forget)

    if (currentScreen == SCREEN_MAIN_MENU) {
        // Execute the online action queued LAST frame — its "DANG KET NOI"
        // overlay is already on screen, because EOS setup (login + lobby)
        // blocks the thread for a few seconds.
        if (menuOnlinePending != 0) {
            int action = menuOnlinePending;
            menuOnlinePending = 0;
            if (action == 1) { // TAO PHONG
                if (Net_StartHostOnline(roomCode, (int)sizeof(roomCode))) {
                    GameScreen_SetOnlineCode(roomCode);
                    currentScreen = SCREEN_LOBBY;
                    continue;
                }
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "TAO PHONG THAT BAI — XEM LOG TERMINAL");
            } else {           // NHAP MA -> join
                if (Net_JoinOnline(menuJoinInput)) {
                    roomCode[0] = '\0'; // khách không cần hiện mã
                    currentScreen = SCREEN_LOBBY;
                    continue;
                }
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "KHONG VAO DUOC PHONG %s", menuJoinInput);
            }
        }

        Vector2 mousePos = GetMousePosition();

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        Rectangle btnSandbox = { sw/2 - 150, sh/2 - 60, 300, 50 };
        Rectangle btnVFX = { sw/2 - 150, sh/2 + 20, 300, 50 };
        Rectangle btnGame = { sw/2 - 150, sh/2 + 100, 300, 50 };
        Rectangle btnHost = { sw/2 - 150, sh/2 + 180, 300, 50 };
        Rectangle btnJoin = { sw/2 - 150, sh/2 + 260, 300, 50 };

        // Robust Android tap: arm the button under the touch while it's DOWN, fire on RELEASE if
        // the release settles over the same button. IsMouseButtonPressed (down-edge) is unreliable
        // here — on the down frame GetMousePosition can still be the stale/previous position, so the
        // over-button test misses even though the button highlights. Same fix pattern as the sandbox
        // top buttons (sandbox/ui_panel.c). Buttons are screen-centered, so no top-edge gesture zone.
        Rectangle menuBtns[5] = { btnSandbox, btnVFX, btnGame, btnHost, btnJoin };
        bool downNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        int overIdx = -1;
        for (int i = 0; i < 5; i++)
            if (CheckCollisionPointRec(mousePos, menuBtns[i])) { overIdx = i; break; }
        static int s_menuArmed = -1;
        if (downNow && overIdx >= 0) s_menuArmed = overIdx;
        int fired = -1;
        if (!downNow && s_menuArmed >= 0) {
            if (overIdx == s_menuArmed) fired = s_menuArmed;
            s_menuArmed = -1;
        }

        if (fired == 0) {
            currentScreen = SCREEN_SKILL_SANDBOX;
        }
        if (fired == 1) {
            currentScreen = SCREEN_VFX_TESTER;
        }
        if (fired == 2) {
            GameScreen_SetMode(GAME_MODE_BOSS); // offline entry — boss match
            currentScreen = SCREEN_GAME;
        }
        if (fired == 3) {
            if (!Net_OnlineAvailable())
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "BUILD CHUA BAT EOS — cmake -DWUXING_EOS=ON");
            else if (Net_GetMode() == NET_MODE_OFF) {
                menuOnlineMsg[0] = '\0';
                menuJoinOpen = false;
                menuOnlinePending = 1;
            }
        }
        if (fired == 4) {
            if (!Net_OnlineAvailable())
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "BUILD CHUA BAT EOS — cmake -DWUXING_EOS=ON");
            else {
                menuJoinOpen = !menuJoinOpen;
                menuOnlineMsg[0] = '\0';
            }
        }

        // Room-code entry (open while the NHAP MA button is toggled on).
        if (menuJoinOpen) {
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ch >= 'a' && ch <= 'z') ch -= 32;
                if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) &&
                    menuJoinLen < 5) {
                    menuJoinInput[menuJoinLen++] = (char)ch;
                    menuJoinInput[menuJoinLen] = '\0';
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && menuJoinLen > 0)
                menuJoinInput[--menuJoinLen] = '\0';
            if (IsKeyPressed(KEY_ENTER) && menuJoinLen >= 3 &&
                Net_GetMode() == NET_MODE_OFF) {
                menuOnlineMsg[0] = '\0';
                menuOnlinePending = 2;
            }
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

        // Online (EOS) — greyed-out label when the build carries the stub.
        bool onlineUp = Net_OnlineAvailable();
        Color onlineTxt = onlineUp ? BLACK : (Color){ 90, 90, 90, 255 };
        DrawRectangleRounded(btnHost, 0.2f, 10, CheckCollisionPointRec(mousePos, btnHost) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnHost, 0.2f, 10, WHITE);
        DrawText("4. TAO PHONG ONLINE", (int)btnHost.x + 45, (int)btnHost.y + 15, 20, onlineTxt);

        DrawRectangleRounded(btnJoin, 0.2f, 10, (menuJoinOpen || CheckCollisionPointRec(mousePos, btnJoin)) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnJoin, 0.2f, 10, WHITE);
        DrawText("5. NHAP MA VAO PHONG", (int)btnJoin.x + 40, (int)btnJoin.y + 15, 20, onlineTxt);

        if (menuJoinOpen) {
            // Entry box to the right of the join button: MA: ABC_ + ENTER hint.
            Rectangle box = { btnJoin.x + btnJoin.width + 16, btnJoin.y, 190, 50 };
            DrawRectangleRec(box, (Color){ 25, 25, 35, 255 });
            DrawRectangleLinesEx(box, 2, (Color){ 240, 220, 120, 255 });
            const char *entry = TextFormat("MA: %s%s", menuJoinInput,
                                           (((int)(g_totalElapsed * 2.0f)) & 1) ? "_" : " ");
            DrawText(entry, (int)box.x + 12, (int)box.y + 8, 24, (Color){ 240, 220, 120, 255 });
            DrawText("ENTER DE VAO", (int)box.x + 12, (int)box.y + 34, 12, (Color){ 180, 180, 190, 255 });
        }
        if (menuOnlineMsg[0] != '\0') {
            int mw = MeasureText(menuOnlineMsg, 18);
            DrawText(menuOnlineMsg, sw/2 - mw/2, (int)btnJoin.y + 60, 18, (Color){ 235, 140, 90, 255 });
        }
        if (menuOnlinePending != 0) {
            // This frame queued a blocking EOS call — tell the user before
            // the window freezes for the few seconds it takes.
            const char *t = (menuOnlinePending == 1) ? "DANG TAO PHONG QUA EPIC..."
                                                     : "DANG TIM PHONG QUA EPIC...";
            int tw2 = MeasureText(t, 26);
            DrawRectangle(sw/2 - tw2/2 - 20, sh/2 - 230, tw2 + 40, 44, (Color){ 15, 15, 25, 230 });
            DrawText(t, sw/2 - tw2/2, sh/2 - 220, 26, (Color){ 240, 220, 120, 255 });
        }

        EndDrawing();
        continue;
    }

    if (currentScreen == SCREEN_LOBBY) {
        // Room screen (Đợt A2). Net_Tick must keep pumping here — peers
        // join/leave and the roster updates while everyone waits.
        Net_Tick(dt);

        // Dev: WUXING_LOBBY_AUTOSTART=<sec> — headless/scripted runs can't
        // click BAT DAU; the host fires it automatically after N seconds.
        static float s_lobbyElapsed = 0.0f;
        s_lobbyElapsed += dt;
        const char *autoStart = getenv("WUXING_LOBBY_AUTOSTART");
        if (autoStart != NULL && Net_GetMode() == NET_MODE_HOST &&
            s_lobbyElapsed >= (float)atoi(autoStart)) {
            // WUXING_LOBBY_BOTS=n — headless runs can't click the bot
            // slots either; drop n bots on side 1 right before starting.
            const char *botEnv = getenv("WUXING_LOBBY_BOTS");
            for (int b = 0; botEnv != NULL && b < atoi(botEnv); b++)
                Net_HostAddBot(1);
            Net_HostStartMatch();
            s_lobbyElapsed = -1e9f; // fire once
        }

        if (Net_ConsumeMatchStart()) {
            // Net rooms play team battle (Đợt A3); WUXING_NET_BOSS=1 keeps
            // the old invasion-vs-boss run for dev/testing.
            GameScreen_SetMode(getenv("WUXING_NET_BOSS") != NULL
                                   ? GAME_MODE_BOSS : GAME_MODE_TEAM_BATTLE);
            GameScreen_Init(&player); // fresh match state for everyone
            currentScreen = SCREEN_GAME;
            continue;
        }

        BeginDrawing();
        ClearBackground((Color){ 12, 12, 20, 255 });
        UILobbyAction act = UI_LobbyUpdateDraw(roomCode,
                                               Net_GetMode() == NET_MODE_HOST);
        EndDrawing();

        if (act == UI_LOBBY_START) {
            Net_HostStartMatch(); // ConsumeMatchStart picks it up next frame
        } else if (act == UI_LOBBY_LEAVE ||
                   (Net_GetMode() == NET_MODE_OFF)) { // host vanished / stopped
            Net_Stop();
            GameScreen_SetOnlineCode(NULL);
            roomCode[0] = '\0';
            currentScreen = SCREEN_MAIN_MENU;
        }
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
        // Spatial-audio ears follow the local player; night bed loops (both
        // no-ops until assets/audio/ has files — Audio_PlayMusic self-guards
        // against restarting the same track).
        Audio_SetListener(player.position);
        Audio_PlayMusic(MUS_ARENA_NIGHT);
        GameScreen_Update(&player, &camera, dt);
        // Dev: WUXING_TEAM_TEST=1 — scripted team-battle round on the host
        // for headless net verification (no inputs available): 10s into
        // FIGHTING wipe side 1 (elimination fires → VICTORY/DEFEAT sync),
        // 6s later run the exact ENTER-rematch path (respawn + reset).
        if (getenv("WUXING_TEAM_TEST") != NULL &&
            GameScreen_GetMode() == GAME_MODE_TEAM_BATTLE &&
            Net_GetMode() == NET_MODE_HOST) {
            static float s_ttClock = 0.0f;
            static bool s_ttWiped = false, s_ttRematched = false;
            s_ttClock += dt;
            if (!s_ttWiped && s_ttClock >= 10.0f &&
                GameScreen_GetState() == GAME_FIGHTING) {
                for (int i = 0; i < MAX_AGENTS; i++) {
                    const Agent *a = Entity_GetAgent(i);
                    if (a != NULL && a->archetype == ARCH_HERO && a->team == TEAM_ENEMY)
                        Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 });
                }
                s_ttWiped = true;
                TraceLog(LOG_INFO, "[TEAMTEST] wiped side 1");
            }
            if (s_ttWiped && !s_ttRematched && s_ttClock >= 16.0f) {
                Net_HostRespawnPeerHeroes();
                GameScreen_Init(&player);
                s_ttRematched = true;
                TraceLog(LOG_INFO, "[TEAMTEST] rematch fired");
            }
        }
        if (GameScreen_RequestedBackToMenu()) {
            currentScreen = SCREEN_MAIN_MENU;
            // Leaving a net match tears the session down (EOS: closes P2P +
            // leaves the lobby) so TAO PHONG / NHAP MA work again from the menu.
            Net_Stop();
            GameScreen_SetOnlineCode(NULL);
            // The match pinned VERDANT_PATH + its ring-out bounds — hand the
            // sandbox its DEFAULT_ARENA world back (bounds are global; a
            // sandbox player outside the match circle would fall forever).
            for (int i = 0; i < MapManager_GetCount(); i++) {
                if (strcmp(MapManager_GetName(i), "DEFAULT_ARENA") == 0) {
                    MapManager_SetActiveIndex(i);
                    break;
                }
            }
            Entity_SetArenaBounds((Vector3){ 6.0f, 0.0f, 4.4f }, 18.0f);
            player.position = (Vector3){ -11.0f, 0.0f, 4.4f }; // sandbox home
            Entity_SetPosition(player.agentId, player.position);
        }
        CameraFX_Update(&camera, dt);
    }

    // Boss AI then Đấu Pháp resolve — boss casts submit through skills into
    // the combat registry, so Boss_Update runs first; Combat_Update last,
    // after all skill updates submitted this frame's projectile colliders
    // (immediate mode). Both tick for every screen; no boss / no
    // submissions = no-op. (Module 7 game/ will own this ordering.)
    // Net transport pump — host applies remote intents / broadcasts
    // snapshots; a connected client mirrors the host pool instead of
    // simulating (Net_ClientDrivesWorld skips the local gameplay ticks).
    Net_Tick(dt);

    // Entities tick — owned HERE for every screen (it used to live inside
    // UpdateSandbox only, so in SCREEN_GAME timers never ticked: one dash
    // arm froze movement forever, jumps never landed, mana never regened,
    // knockback/ring-out physics were dead). Runs after the screen updates
    // (positions pushed) and before AI/boss/combat consume fresh state.
    if (!Net_ClientDrivesWorld()) {
        Entity_Update(dt);
        AI_Update(dt);
        Boss_Update(dt);
        Formation_Update(dt);
        Combat_Update(dt);
    }

    // Hero-bot casts → net mirror (Đợt A5): ai/ is net-blind, so main.c
    // ferries its cast events to connected clients (no-op offline).
    {
        HeroBotCast botCasts[16];
        int nCasts = AI_PollHeroCasts(botCasts, 16);
        for (int ci = 0; ci < nCasts; ci++)
            Net_HostNotifyCast(botCasts[ci].agentId, botCasts[ci].skillIndex,
                               botCasts[ci].aim);
    }

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
            Audio_PlaySFXAt(SFX_EXPLOSION, booms[bi].pos);
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
    Atmosphere_Update(dt, camera); // G3 — drift dust motes

    SkillDebugger_PreRender();

    BeginDrawing();

    // Real Shading P6 — depth-only shadow caster pre-pass, off by default.
    // Re-invokes the same (pure, no-side-effect) draw functions used for the
    // real scene below, but into the light's depth target with the depth-only
    // shader; overlay draws (HP bars, decals) get swept in too since these
    // functions aren't split into geometry-only vs. UI layers — harmless
    // (they just contribute stray depth), not visually wrong.
    if (EnvShadow_IsEnabled()) {
        EnvShadow_BeginCapture();
        Model charModel = CharacterModel_GetModel();
        if (CharacterModel_IsLoaded()) {
            SurfaceMaterial_BeginShadowCast(charModel, EnvShadow_GetDepthShader());
        }
        if (!g_isDebuggerCapturing && currentScreen == SCREEN_SKILL_SANDBOX) {
            DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
        }
        if (currentScreen == SCREEN_GAME) {
            GameScreen_Draw3D(&player);
            Boss_Draw();
            Formation_Draw();
        }
        if (CharacterModel_IsLoaded()) {
            SurfaceMaterial_EndShadowCast(charModel);
        }
        EnvShadow_EndCapture();
    }

    ScreenDistort_Begin();
    if (g_isDebuggerCapturing) {
        ClearBackground(BLACK);
    } else {
        ClearBackground(GetColor(0x111111FF));
    }

    MyBeginMode3D(camera);
    SurfaceMaterial_UpdateFrame(camera); // G2 — push sun/ambient/fog to lit models
    GroundShadow_UpdateFrame(); // Real Shading P6 — push shadow map to raw-immediate ground draws
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
        Formation_Draw();
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

    Atmosphere_Draw(camera); // G3 — ambient dust motes (additive, in HDR scene)

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
            static const char *gfxTierName[4] = { "UNLIT", "LOW", "MED", "HIGH" };
            DrawText(TextFormat("GFX [L]: %s   SHADOW [J]: %s", gfxTierName[GfxQuality_Get()],
                                 EnvShadow_IsEnabled() ? "ON" : "OFF"), 10, 662, 20, SKYBLUE);
        }

        // Real Shading P6 — debug preview of the raw shadow-map texture
        // (temporary, remove once the pipeline is confirmed working). If
        // this box is a flat solid color, the depth CAPTURE isn't producing
        // real per-pixel data; if it shows a recognizable arena/character
        // silhouette, the capture is fine and the bug is in the comparison
        // math instead.
        if (EnvShadow_IsEnabled()) {
            int previewSize = 220;
            int px = GetScreenWidth() - previewSize - 10;
            int py = GetScreenHeight() - previewSize - 10;
            Texture2D shadowTex = EnvShadow_GetShadowMap();
            DrawRectangle(px - 2, py - 2, previewSize + 4, previewSize + 4, BLACK);
            DrawTexturePro(shadowTex,
                           (Rectangle){ 0, 0, (float)shadowTex.width, (float)shadowTex.height },
                           (Rectangle){ (float)px, (float)py, (float)previewSize, (float)previewSize },
                           (Vector2){ 0, 0 }, 0.0f, WHITE);
            DrawText("SHADOW MAP DEBUG", px, py - 20, 14, YELLOW);
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
    if (shadowTestMode) {
        shadowTestFrame++;
        if (shadowTestFrame >= shadowTestWarmup) {
            if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
            Image img = LoadImageFromScreen();
            ExportImage(img, "autotest_output/shadow_test.png");

            // Parts 8-10's screen-pixel numeric readback (scanning ground_shadow.fs's own
            // output color as encoded data) proved unreliable — values didn't vary
            // consistently with world position, likely HDR bloom bleed from nearby glowing
            // sandbox props or the scan path crossing other rendered geometry, not pure
            // ground_shadow.fs pixels. Replaced with EnvShadow_DebugDump (the proven H-hotkey
            // CPU diagnostic) plus direct TraceLogs of the exact Matrix bytes at both the
            // computation site (env_shadow.c) and the upload site (ground_shadow.c) — see
            // REAL_SHADING_P6_NOTES.md session-4 part 11 for how to read the combined output.
            EnvShadow_DebugDump(player.position);
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
  Atmosphere_Unload();
  MetaballFX_Unload();
  UnloadSkillManager();
  DamageVolume_Unload();
  EmitterSystem_Unload();
  ResourceManager_Unload();
  MapManager_Unload();
  Audio_Shutdown();
  CloseWindow();

  return exitCode;
}