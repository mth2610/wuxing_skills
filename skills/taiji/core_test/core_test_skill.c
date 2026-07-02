#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/screen_distort.h"
#include "core/procedural_mesh_utils.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/debug_draw.h"
#include "sandbox/auto_test.h"
#include "raymath.h"
#include "rlgl.h"

#include <stddef.h>
#include <stdio.h>

// CORE_ISSUES.md Item 3 (Soft Particles) test shape — a sphere half-buried
// at ground level. Verifies ScreenDistort_BindDepthForSoftParticles fades
// the buried bottom smoothly instead of hard-clipping against the ground.
// Confirmed-done features get removed once tested — keep this file minimal.

// Bumped from 30 to 200 as a diagnostic: debug view showed a perfectly
// uniform "fully opaque" gray across the whole sphere with zero gradient
// even at the ground-contact rim, on real ground (not the earlier void
// area) — testing whether 30 was just too small a window for this 40-radius
// sphere's real depth range, or the fade mechanism itself isn't reaching
// the shader (CORE_ISSUES.md Item 3).
#define CORE_TEST_SOFT_FADE_DISTANCE 200.0f

extern Camera3D camera;

static Shader s_softShader;
static int s_fadeDistLoc = -1;
static int s_debugShowFadeLoc = -1;
static bool s_shaderLoaded = false;
static bool s_testActive = false;
// CORE_ISSUES.md Item 15 — owner identity, set from Cast's agentId param.
static int s_ownerAgentId = -1;
// 0 = normal shaded, 1 = clamped SoftParticle_Factor grayscale, 2 = raw
// unclamped diff (green=positive/unoccluded, red=negative/occluded).
static int s_debugMode = 0;
static Vector3 s_spherePos = {0};
static float s_sphereRadius = 40.0f;

#define CORE_TEST_SOFT_SAMPLE_COUNT 3
static struct {
  const char *label;
  bool valid;
  float sceneLinear;
  float fragLinear;
  float diff;
} s_readback[CORE_TEST_SOFT_SAMPLE_COUNT] = {
  { "top", false, 0, 0, 0 },
  { "mid", false, 0, 0, 0 },
  { "bottom", false, 0, 0, 0 },
};
static bool s_hasReadback = false;

// Reads prevDepthTex on the CPU (numeric, not screenshot-color-guessing —
// see CORE_ISSUES.md Item 3's process note) and caches results for both
// TraceLog, DrawCoreTestSkillDebugHUD() (stdout isn't always visible), and
// the autotest bridge (CoreTestSkill_GetReadback).
void CoreTestSkill_TriggerReadback(void) {
  Texture2D depthTex = ScreenDistort_GetDepthTexture();
  if (depthTex.id == 0) return;

  Image depthImg = LoadImageFromTexture(depthTex);
  if (depthImg.data == NULL || depthImg.format != PIXELFORMAT_UNCOMPRESSED_R32) {
    TraceLog(LOG_WARNING, "[CORE_TEST SOFT] depth readback failed (unexpected format %d)",
             depthImg.format);
    UnloadImage(depthImg);
    return;
  }
  const float *depthData = (const float *)depthImg.data;

  Vector3 sampleWorldPos[CORE_TEST_SOFT_SAMPLE_COUNT] = {
    { s_spherePos.x, s_spherePos.y + s_sphereRadius * 0.9f, s_spherePos.z },
    { s_spherePos.x, s_spherePos.y,                          s_spherePos.z },
    { s_spherePos.x, s_spherePos.y - s_sphereRadius * 0.9f, s_spherePos.z },
  };

  for (int i = 0; i < CORE_TEST_SOFT_SAMPLE_COUNT; i++) {
    Vector2 screenPos = GetWorldToScreen(sampleWorldPos[i], camera);
    int px = (int)screenPos.x;
    int py = (int)screenPos.y;
    if (px < 0 || py < 0 || px >= depthImg.width || py >= depthImg.height) {
      s_readback[i].valid = false;
      TraceLog(LOG_INFO, "[CORE_TEST SOFT] %s: off-screen (%.0f, %.0f)",
               s_readback[i].label, screenPos.x, screenPos.y);
      continue;
    }
    float sceneLinear = depthData[py * depthImg.width + px];
    float fragLinear = Vector3Distance(camera.position, sampleWorldPos[i]);
    s_readback[i].valid = true;
    s_readback[i].sceneLinear = sceneLinear;
    s_readback[i].fragLinear = fragLinear;
    s_readback[i].diff = sceneLinear - fragLinear;
    TraceLog(LOG_INFO, "[CORE_TEST SOFT] %s: scene=%.2f frag=%.2f diff=%.2f",
             s_readback[i].label, sceneLinear, fragLinear, s_readback[i].diff);
  }

  s_hasReadback = true;
  UnloadImage(depthImg);
}

// Autotest case: frame 0 activates the sphere at the arena center; frame 3
// (one full draw + ScreenDistort_SnapshotDepth cycle after activation)
// triggers the readback and checks the buried "bottom" sample. Expected to
// FAIL right now — CORE_ISSUES.md Item 3's ground-occlusion bug is still
// open — this documents the regression numerically and flips to PASS
// automatically once that bug is actually fixed.
static AutoTestResult CoreTestSkill_AutoTestStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    CoreTestSkill_ForceActivate(0, (Vector3){600.0f, 0.0f, 440.0f});
    return AUTOTEST_RUNNING;
  }
  if (frameInCase == 3) {
    CoreTestSkill_TriggerReadback();
    float diff;
    if (!CoreTestSkill_GetReadback(2 /* "bottom" */, NULL, NULL, &diff)) {
      snprintf(outReason, outReasonSize, "bottom sample off-screen or invalid");
      return AUTOTEST_FAIL;
    }
    return AutoTest_ExpectFloatNear(diff, 0.0f, 5.0f, "buried-bottom depth fade", outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

// CORE_ISSUES.md Item 12 (VFX Light eviction) autotest: fill the whole
// MAX_VFX_LIGHTS pool with VFX_PRIORITY_LOW lights, then spawn one more with
// VFX_PRIORITY_HIGH_ULTIMATE. Expect the pool to still report exactly
// MAX_VFX_LIGHTS active lights (one LOW slot got evicted for the ultimate),
// proving the high-priority spawn wasn't silently dropped.
static AutoTestResult CoreTestSkill_VFXLightEvictionStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    VFXLight_Reset();
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
      VFXLight_Spawn((Vector3){600.0f, 0.0f, 440.0f}, WHITE, 20.0f, 5.0f, VFX_PRIORITY_LOW);
    }
    VFXLight_Spawn((Vector3){600.0f, 0.0f, 440.0f}, RED, 30.0f, 5.0f, VFX_PRIORITY_HIGH_ULTIMATE);

    VFXLightData active[MAX_VFX_LIGHTS];
    int count = 0;
    VFXLight_GetActive(active, &count, MAX_VFX_LIGHTS);

    bool foundUltimate = false;
    for (int i = 0; i < count; i++) {
      if (active[i].radius > 25.0f) foundUltimate = true;
    }

    if (count != MAX_VFX_LIGHTS) {
      snprintf(outReason, outReasonSize, "expected %d active lights, got %d", MAX_VFX_LIGHTS, count);
      return AUTOTEST_FAIL;
    }
    return AutoTest_ExpectTrue(foundUltimate, "high-priority light evicted a low-priority slot instead of being dropped",
                               outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

// CORE_ISSUES.md Item 12 (Trail eviction) autotest: same idea as the VFX
// light case above, but for the trail pool. Fill MAX_TRAIL_PARTICLES with
// VFX_PRIORITY_LOW trails (finite lifetime — note TrailConfig.life==0 is
// killed on the very next UpdateTrailSystem tick, NOT "persistent" despite
// the doc comment, so a real duration is used here to avoid a false pass),
// then spawn one VFX_PRIORITY_HIGH_ULTIMATE trail — expect a valid (non -1)
// id, proving eviction happened instead of outright rejection.
static AutoTestResult CoreTestSkill_TrailEvictionStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
      TrailConfig cfg = {0};
      cfg.type = TRAIL_TYPE_PORTAL;
      cfg.pos = (Vector3){600.0f, 0.0f, 440.0f};
      cfg.len = 10.0f;
      cfg.thick = 5.0f;
      cfg.life = 30.0f;
      cfg.tint = WHITE;
      cfg.priority = VFX_PRIORITY_LOW;
      SpawnTrailEntity(cfg);
    }

    TrailConfig ultimateCfg = {0};
    ultimateCfg.type = TRAIL_TYPE_PORTAL;
    ultimateCfg.pos = (Vector3){600.0f, 0.0f, 440.0f};
    ultimateCfg.len = 10.0f;
    ultimateCfg.thick = 5.0f;
    ultimateCfg.life = 30.0f;
    ultimateCfg.tint = RED;
    ultimateCfg.priority = VFX_PRIORITY_HIGH_ULTIMATE;
    int ultimateId = SpawnTrailEntity(ultimateCfg);

    return AutoTest_ExpectTrue(ultimateId != -1, "high-priority trail evicted a low-priority slot instead of being rejected",
                               outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

// CORE_ISSUES.md Item 16 (cooldown gating state) autotest: use a high,
// unused skillIndex slot (MAX_SKILLS - 1) so this doesn't collide with any
// real registered skill's cooldown. Trigger a 5s cooldown for agentId=0,
// check SkillManager_CanCast(agentId=0) is false — AND, the actual point of
// Item 16's per-agent upgrade, that agentId=1 is completely unaffected
// (proves cooldowns don't leak across casters sharing the same skill).
#define CORE_TEST_COOLDOWN_SKILL_INDEX 31
static AutoTestResult CoreTestSkill_CooldownGatingStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    SkillManager_TriggerCooldown(CORE_TEST_COOLDOWN_SKILL_INDEX, 0, 5.0f);
    bool casterOnCooldown = SkillManager_CanCast(CORE_TEST_COOLDOWN_SKILL_INDEX, 0);
    bool otherCasterFree = SkillManager_CanCast(CORE_TEST_COOLDOWN_SKILL_INDEX, 1);
    if (!AutoTest_ExpectTrue(!casterOnCooldown, "SkillManager_CanCast(agentId=0) should be false right after that agent's TriggerCooldown",
                             outReason, outReasonSize)) {
      return AUTOTEST_FAIL;
    }
    return AutoTest_ExpectTrue(otherCasterFree, "SkillManager_CanCast(agentId=1) should stay true — another caster's cooldown must not leak",
                               outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

// CORE_ISSUES.md Item 14 (abort/interrupt) autotest: register an abort
// callback for a scratch skillIndex, call AbortSkill() for a specific
// agentId, verify the callback ran AND received that same agentId (the
// point of Item 14's per-agent upgrade — a skill's callback can tell which
// caster's instance to actually abort instead of nuking every instance).
#define CORE_TEST_ABORT_SKILL_INDEX 30
static bool s_abortCallbackRan = false;
static int s_abortCallbackAgentId = -1;
static void CoreTestSkill_AbortCallback(int agentId) {
  s_abortCallbackRan = true;
  s_abortCallbackAgentId = agentId;
}

static AutoTestResult CoreTestSkill_AbortRegistrationStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    s_abortCallbackRan = false;
    s_abortCallbackAgentId = -1;
    RegisterSkillAbort(CORE_TEST_ABORT_SKILL_INDEX, CoreTestSkill_AbortCallback);
    AbortSkill(CORE_TEST_ABORT_SKILL_INDEX, 7);
    if (!AutoTest_ExpectTrue(s_abortCallbackRan, "AbortSkill should invoke the registered abort callback",
                             outReason, outReasonSize)) {
      return AUTOTEST_FAIL;
    }
    return AutoTest_ExpectTrue(s_abortCallbackAgentId == 7, "abort callback should receive the agentId AbortSkill was called with",
                               outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  s_softShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_soft.vs",
      "skills/taiji/core_test/core_test_soft.fs");
  s_fadeDistLoc = GetShaderLocation(s_softShader, "u_fadeDistance");
  s_debugShowFadeLoc = GetShaderLocation(s_softShader, "u_debugShowFade");
  s_shaderLoaded = (s_softShader.id != 0);
  if (AutoTest_IsEnabled()) {
      AutoTest_Register("soft_particle_ground_fade", CoreTestSkill_AutoTestStep, 10);
      AutoTest_Register("vfx_light_priority_eviction", CoreTestSkill_VFXLightEvictionStep, 5);
      AutoTest_Register("trail_priority_eviction", CoreTestSkill_TrailEvictionStep, 5);
      AutoTest_Register("skill_cooldown_gating", CoreTestSkill_CooldownGatingStep, 5);
      AutoTest_Register("skill_abort_registration", CoreTestSkill_AbortRegistrationStep, 5);
  }
}

// Sphere center at ground level (Y=0) — half buried, half exposed, in an
// open area with no occluder in front (per Item 3's open question). Shared
// by the real Cast path and the autotest bridge below.
void CoreTestSkill_ForceActivate(int agentId, Vector3 spherePos) {
  s_spherePos = spherePos;
  s_testActive = true;
  s_ownerAgentId = agentId;
}

bool CoreTestSkill_GetReadback(int sampleIndex, float *outSceneLinear, float *outFragLinear, float *outDiff) {
  if (sampleIndex < 0 || sampleIndex >= CORE_TEST_SOFT_SAMPLE_COUNT) return false;
  if (!s_hasReadback || !s_readback[sampleIndex].valid) return false;
  if (outSceneLinear) *outSceneLinear = s_readback[sampleIndex].sceneLinear;
  if (outFragLinear) *outFragLinear = s_readback[sampleIndex].fragLinear;
  if (outDiff) *outDiff = s_readback[sampleIndex].diff;
  return true;
}

void CastCoreTestSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
  (void)target;
  (void)params;
  CoreTestSkill_ForceActivate(agentId, (Vector3){ startPos.x, 0.0f, startPos.z });
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)dt;
  (void)enemyPos;
  (void)enemyRadius;
  if (s_testActive && IsKeyPressed(KEY_L)) {
    CoreTestSkill_TriggerReadback();
  }
  // NOTE: KEY_K is already bound in main.c to cycle maps — using it here
  // too silently switched the whole map on every debug-view toggle, which
  // is what looked like the sphere's color depending on player position.
  if (s_testActive && IsKeyPressed(KEY_H)) {
    s_debugMode = (s_debugMode + 1) % 3;
  }
}

void DrawCoreTestSkill(void) {
  if (!s_testActive || !s_shaderLoaded) return;

  BeginShaderMode(s_softShader);
  SkillManager_BeginShader(s_softShader);
  if (s_fadeDistLoc >= 0) {
    float fade = CORE_TEST_SOFT_FADE_DISTANCE;
    SetShaderValue(s_softShader, s_fadeDistLoc, &fade, SHADER_UNIFORM_FLOAT);
  }
  if (s_debugShowFadeLoc >= 0) {
    float debugFlag = (float)s_debugMode;
    SetShaderValue(s_softShader, s_debugShowFadeLoc, &debugFlag, SHADER_UNIFORM_FLOAT);
  }
  ScreenDistort_BindDepthForSoftParticles(s_softShader, 3);

  BeginBlendMode(BLEND_ALPHA);
  rlDisableDepthTest();
  rlDisableDepthMask();
  DrawCoreSphere(s_spherePos, s_sphereRadius, 24, 24, ELEMENT_COLOR_TAIJI);
  rlEnableDepthMask();
  rlEnableDepthTest();
  EndBlendMode();

  ScreenDistort_UnbindSoftParticleDepth(3);
  SkillManager_EndShader();
}

void UnloadCoreTestSkill(void) {}

bool IsCoreTestSkillCoiling(void) { return false; }

int GetCoreTestSkillProjectiles(SkillProjectile *outProjectiles,
                                int maxProjectiles) {
  (void)outProjectiles;
  (void)maxProjectiles;
  return 0;
}

void DeactivateCoreTestProjectile(int index) { (void)index; }

void DrawCoreTestSkillDebugHUD(void) {
  if (!s_testActive) return;

  int x = 650, y = 10;
  DrawRectangle(x - 6, y - 4, 540, 160, ColorAlpha(BLACK, 0.55f));
  DrawText("CORE_TEST SOFT PARTICLE (Item 3) - L: sample  H: cycle debug mode", x, y, 16, YELLOW);
  y += 22;
  const char *modeText;
  Color modeColor;
  switch (s_debugMode) {
    case 1: modeText = "MODE 1: factor grayscale - gray=opaque(1.0) black=faded(0.0)"; modeColor = GREEN; break;
    case 2: modeText = "MODE 2: raw diff - green=unoccluded red=occluded YELLOW=near-zero(<10)"; modeColor = SKYBLUE; break;
    default: modeText = "MODE 0: normal shaded look"; modeColor = LIGHTGRAY; break;
  }
  DrawText(modeText, x, y, 16, modeColor);
  y += 22;

  if (!s_hasReadback) {
    DrawText("No sample yet - press L while the sphere is visible", x, y, 16, LIGHTGRAY);
    return;
  }

  for (int i = 0; i < CORE_TEST_SOFT_SAMPLE_COUNT; i++) {
    if (!s_readback[i].valid) {
      DrawText(TextFormat("%s: off-screen", s_readback[i].label), x, y, 16, RED);
    } else {
      DrawText(TextFormat("%s: scene=%.2f  frag=%.2f  diff=%.2f",
                          s_readback[i].label, s_readback[i].sceneLinear,
                          s_readback[i].fragLinear, s_readback[i].diff),
               x, y, 16, WHITE);
    }
    y += 22;
  }
}
