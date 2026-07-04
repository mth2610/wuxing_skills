#include "skills/taiji/core_test/core_test_skill.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/screen_distort.h"
#include "core/procedural_mesh_utils.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/debug_draw.h"
#include "core/skill_helper.h"
#include "core/vfx_proc_ray.h"
#include "core/particle_system.h"
#include "core/tuning.h"
#include "sandbox/auto_test.h"
#include "entities/entities.h"
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
#define CORE_TEST_SOFT_FADE_DISTANCE 0.3f

extern Camera3D camera;

static Shader s_softShader;
static int s_fadeDistLoc = -1;
static int s_debugShowFadeLoc = -1;
static int s_lightDirLoc = -1;
static bool s_shaderLoaded = false;
static bool s_testActive = false;
static bool s_showSoftParticleSphere = false; // only true via ForceActivate (autotest), not the interactive Cast path
// CORE_ISSUES.md Item 15 — owner identity, set from Cast's agentId param.
static int s_ownerAgentId = -1;
// 0 = normal shaded, 1 = clamped SoftParticle_Factor grayscale, 2 = raw
// unclamped diff (green=positive/unoccluded, red=negative/occluded).
static int s_debugMode = 0;
static Vector3 s_spherePos = {0};
static float s_sphereRadius = 0.4f;

static int s_coreTestSkillIndex = -1;

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
    Vector3 spherePos = { camera.target.x + 2.0f, 0.0f, camera.target.z };
    CoreTestSkill_ForceActivate(0, spherePos);
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

// CORE_ISSUES.md Item 13 (lifecycle-end query) autotest: cast STONE_PRISON
// for agentId=0 through the real CastSkill() dispatch, verify
// Skill_HasActiveInstance reports true for that caster and false for an
// unrelated agentId — proves per-caster isolation (registered before the
// STONE_PRISON smoke test below so MAX_PRISONS still has a free slot).
static AutoTestResult CoreTestSkill_LifecycleQueryStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    int idx = Skill_GetIndexByName("STONE_PRISON");
    if (idx < 0) {
      snprintf(outReason, outReasonSize, "STONE_PRISON not found via Skill_GetIndexByName");
      return AUTOTEST_FAIL;
    }
    SkillParams params = {0};
    params.level = 1;
    params.sizeScale = 1.0f;
    params.quantity = 1;
    params.damage = 10.0f;
    CastSkill(idx, 0, (Vector3){600.0f, 0.0f, 440.0f}, (Vector3){650.0f, 0.0f, 440.0f}, params);
    if (!AutoTest_ExpectTrue(Skill_HasActiveInstance(idx, 0), "Skill_HasActiveInstance(idx,0) should be true right after that agent casts STONE_PRISON",
                             outReason, outReasonSize)) {
      return AUTOTEST_FAIL;
    }
    return AutoTest_ExpectTrue(!Skill_HasActiveInstance(idx, 1), "Skill_HasActiveInstance(idx,1) should stay false — another agent's instance must not leak",
                               outReason, outReasonSize)
               ? AUTOTEST_PASS
               : AUTOTEST_FAIL;
  }
  return AUTOTEST_RUNNING;
}

// CORE_ISSUES.md Item 17 (debug draw) autotest: DebugDraw_Sphere runs inside
// the real 3D draw pass (DrawCoreTestSkill), not from this step function
// (which executes after EndDrawing()) — bridge via a static flag toggled
// here and read back by DrawCoreTestSkill, verified via a numeric pixel
// count (not a screenshot eyeball — see Item 3's false-positive history for
// why numeric-only). Drawn at camera.target so it always projects near
// screen center regardless of camera-follow behavior.
static bool    s_debugDrawTestActive = false;
static Vector3 s_debugDrawTestPos = {0};
// Real-world-scaled (÷100, see root CLAUDE.md) — must stay well under the
// current camera-to-target distance (g_camDist in sandbox_core.c, ~6-15
// units post-rescale) or the camera sits inside the sphere and it renders
// as nothing (backface-culled), which reads as a false test failure.
#define CORE_TEST_DEBUG_DRAW_RADIUS 0.6f
#define CORE_TEST_DEBUG_DRAW_COLOR MAGENTA

static void CoreTestSkill_SetDebugDrawTest(bool active, Vector3 pos) {
  s_debugDrawTestActive = active;
  s_debugDrawTestPos = pos;
}

static int CoreTestSkill_CountMagentaPixels(Vector2 center) {
  Image screenImg = LoadImageFromScreen();
  int count = 0;
  int half = 70;
  int x0 = (int)center.x - half; if (x0 < 0) x0 = 0;
  int x1 = (int)center.x + half; if (x1 > screenImg.width) x1 = screenImg.width;
  int y0 = (int)center.y - half; if (y0 < 0) y0 = 0;
  int y1 = (int)center.y + half; if (y1 > screenImg.height) y1 = screenImg.height;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      Color c = GetImageColor(screenImg, x, y);
      if (c.r > 200 && c.b > 200 && c.g < 60) count++;
    }
  }
  UnloadImage(screenImg);
  return count;
}

static int s_debugDrawDisabledCount = -1;
static AutoTestResult CoreTestSkill_DebugDrawStep(int frameInCase, char *outReason, int outReasonSize) {
  Vector2 screenPos = GetWorldToScreen(camera.target, camera);
  if (frameInCase == 0) {
    DebugDraw_SetEnabled(false);
    CoreTestSkill_SetDebugDrawTest(true, camera.target);
    return AUTOTEST_RUNNING;
  }
  if (frameInCase == 2) {
    s_debugDrawDisabledCount = CoreTestSkill_CountMagentaPixels(screenPos);
    DebugDraw_SetEnabled(true);
    return AUTOTEST_RUNNING;
  }
  if (frameInCase == 4) {
    int enabledCount = CoreTestSkill_CountMagentaPixels(screenPos);
    DebugDraw_SetEnabled(false);
    CoreTestSkill_SetDebugDrawTest(false, (Vector3){0});
    if (!AutoTest_ExpectTrue(s_debugDrawDisabledCount < 3, "DebugDraw_Sphere should draw nothing while disabled", outReason, outReasonSize)) {
      return AUTOTEST_FAIL;
    }
    if (!AutoTest_ExpectTrue(enabledCount > s_debugDrawDisabledCount + 10, "DebugDraw_Sphere should draw visible magenta pixels once enabled", outReason, outReasonSize)) {
      return AUTOTEST_FAIL;
    }
    return AUTOTEST_PASS;
  }
  return AUTOTEST_RUNNING;
}


// Real-skill smoke test: casts an actual gameplay skill through the exact
// same CastSkill() dispatch main.c/sandbox_core.c use (not a synthetic
// scratch index), then confirms two things numerically: (1) the skill is
// actually registered (Skill_GetIndexByName found it — regresses if a skill
// gets renamed/dropped from the registry), and (2) this session's cooldown
// adoption (CORE_ISSUES.md Item 16 "Fix D") actually fires for real skills,
// not just the synthetic core_test case above. Uses agentId=5, distinct
// from any real player/enemy agentId, purely as a scratch caster identity.
static AutoTestResult SkillSmokeTestStep(const char *skillName, int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase == 0) {
    int idx = Skill_GetIndexByName(skillName);
    if (idx < 0) {
      snprintf(outReason, outReasonSize, "%s not found via Skill_GetIndexByName (not registered)", skillName);
      return AUTOTEST_FAIL;
    }
    int testAgentId = 5;
    SkillParams params = {0};
    params.level = 1;
    params.sizeScale = 1.0f;
    params.quantity = 1;
    params.damage = 10.0f;
    CastSkill(idx, testAgentId, (Vector3){600.0f, 0.0f, 440.0f}, (Vector3){650.0f, 0.0f, 440.0f}, params);
    if (SkillManager_CanCast(idx, testAgentId)) {
      snprintf(outReason, outReasonSize, "%s: cooldown gate did not trigger after cast", skillName);
      return AUTOTEST_FAIL;
    }
    return AUTOTEST_RUNNING; // let a few real Update/Draw frames run without crashing
  }
  if (frameInCase >= 5) return AUTOTEST_PASS;
  return AUTOTEST_RUNNING;
}




static AutoTestResult SkillSmokeTest_Fire(int f, char *r, int rs) { return SkillSmokeTestStep("FIRE", f, r, rs); }
static AutoTestResult SkillSmokeTest_Tube(int f, char *r, int rs) { return SkillSmokeTestStep("TUBE", f, r, rs); }
static AutoTestResult SkillSmokeTest_WaterSphere(int f, char *r, int rs) { return SkillSmokeTestStep("WATER_SPHERE", f, r, rs); }
static AutoTestResult SkillSmokeTest_WoodThorns(int f, char *r, int rs) { return SkillSmokeTestStep("WOOD_THORNS", f, r, rs); }
static AutoTestResult SkillSmokeTest_StonePrison(int f, char *r, int rs) { return SkillSmokeTestStep("STONE_PRISON", f, r, rs); }
static AutoTestResult SkillSmokeTest_HoaLongPhongBa(int f, char *r, int rs) { return SkillSmokeTestStep("HOA_LONG_PHONG_BA", f, r, rs); }

// ============================================================================
// Procedural mesh geometry autotests (user request: re-verify
// core/procedural_mesh_utils's Build*() functions produce correct shapes).
// Pure CPU math against the Build output structs directly — no screen
// readback, no camera dependency at all, sidestepping the whole class of
// GetWorldToScreen/camera bugs that plagued Item 3. Each runs entirely in
// frame 0.
// ============================================================================

static AutoTestResult CoreTestSkill_ProceduralTubeStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 p0 = {0, 0, 0}, p1 = {100, 0, 0}, p2 = {200, 0, 0}, p3 = {300, 0, 0};
  float baseRadius = 20.0f;
  int segments = 8, radialSegs = 12;
  TubeMeshData data;
  ProceduralMesh_BuildTube(&data, p0, p1, p2, p3, baseRadius, 1.0f, 0.0f, segments, radialSegs, NULL);

  if (!AutoTest_ExpectFloatNear(Vector3Distance(data.tailCenter, p0), 0.0f, 0.5f,
                                 "tailCenter should equal p0 (straight line, t=0)", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(Vector3Distance(data.headCenter, p3), 0.0f, 0.5f,
                                 "headCenter should equal p3 (straight line, t=1)", outReason, outReasonSize))
    return AUTOTEST_FAIL;

  float maxPerp = 0.0f, minRadius = 1e9f, maxRadius = 0.0f;
  for (int i = 0; i <= segments; i++) {
    float t = (float)i / (float)segments;
    Vector3 pos = ProceduralMesh_BezierPoint(p0, p1, p2, p3, t);
    Vector3 tangent = Vector3Normalize(ProceduralMesh_BezierTangent(p0, p1, p2, p3, t));
    for (int j = 0; j < radialSegs; j++) {
      Vector3 diff = Vector3Subtract(data.rings[i][j], pos);
      float perp = fabsf(Vector3DotProduct(diff, tangent));
      if (perp > maxPerp) maxPerp = perp;
      float r = Vector3Length(diff);
      if (r < minRadius) minRadius = r;
      if (r > maxRadius) maxRadius = r;
    }
  }
  if (!AutoTest_ExpectTrue(maxPerp < 1.0f,
                            "ring vertices should sit in the plane perpendicular to the path tangent, not drift along it",
                            outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectTrue(minRadius > 0.1f && maxRadius < baseRadius * 2.0f,
                            "ring radius should stay within a sane envelope of baseRadius", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralWavePlaneStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 center = {0, 0, 0};
  float width = 200.0f, length = 150.0f;
  int segX = 10, segZ = 8;
  WavePlaneMeshData data;
  ProceduralMesh_BuildWavePlane(&data, center, width, length, segX, segZ, 0.0f, NULL);
  float amplitude = ProceduralMesh_DefaultWavePlaneConfig().amplitude;

  if (!AutoTest_ExpectFloatNear(data.verts[0][0].x, center.x - width * 0.5f, 0.1f,
                                 "grid corner (0,0) should sit at -width/2 on X", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(data.verts[segX][segZ].x, center.x + width * 0.5f, 0.1f,
                                 "grid corner (segX,segZ) should sit at +width/2 on X", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(data.verts[0][0].z, center.z - length * 0.5f, 0.1f,
                                 "grid corner (0,0) should sit at -length/2 on Z", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(data.verts[segX][segZ].z, center.z + length * 0.5f, 0.1f,
                                 "grid corner (segX,segZ) should sit at +length/2 on Z", outReason, outReasonSize))
    return AUTOTEST_FAIL;

  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i <= segX; i++)
    for (int j = 0; j <= segZ; j++) {
      float y = data.verts[i][j].y;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
  if (!AutoTest_ExpectTrue(minY >= center.y - amplitude * 1.3f && maxY <= center.y + amplitude * 1.3f,
                            "wave Y displacement should stay within the amplitude envelope", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectTrue((maxY - minY) > 1.0f,
                            "wave should actually vary in Y, not sit perfectly flat", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralCurlingWaveStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 baseCenter = {0, 0, 0};
  Vector3 widthDir = {1, 0, 0};
  int profileSegs = 8, widthSegs = 8;
  CurlingWaveMeshData data;
  ProceduralMesh_BuildCurlingWave(&data, baseCenter, widthDir, NULL, profileSegs, widthSegs);
  CurlingWaveConfig cfg = ProceduralMesh_DefaultCurlingWaveConfig();

  if (!AutoTest_ExpectFloatNear(data.verts[widthSegs / 2][0].y, 0.0f, 3.0f,
                                 "curling wave base (p=0) should sit near y=0", outReason, outReasonSize))
    return AUTOTEST_FAIL;

  float sweepAngle = PI * 0.5f + PI * 0.5f * cfg.curlAmount;
  float expectedTopY = sinf(-PI * 0.5f + sweepAngle) * cfg.height + cfg.height;
  if (!AutoTest_ExpectFloatNear(data.verts[widthSegs / 2][profileSegs].y, expectedTopY, 4.0f,
                                 "curling wave top (p=profileSegs) should reach the curl-adjusted arch height",
                                 outReason, outReasonSize))
    return AUTOTEST_FAIL;

  if (!AutoTest_ExpectFloatNear(data.verts[0][profileSegs / 2].x, -cfg.archWidth * 0.5f, 0.1f,
                                 "left edge should sit at -archWidth/2", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(data.verts[widthSegs][profileSegs / 2].x, cfg.archWidth * 0.5f, 0.1f,
                                 "right edge should sit at +archWidth/2", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralRockStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 center = {0, 0, 0};
  float radius = 50.0f, jitter = 0.2f;
  RockMeshData data;
  ProceduralMesh_BuildRock(&data, center, radius, jitter, 123, 1);

  if (!AutoTest_ExpectTrue(data.vertCount >= 12 && data.vertCount <= ROCK_MESH_MAX_VERTS,
                            "rock vertex count should be a valid icosphere subdivision result", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectTrue(data.faceCount >= 20 && data.faceCount <= ROCK_MESH_MAX_FACES,
                            "rock face count should be a valid icosphere subdivision result", outReason, outReasonSize))
    return AUTOTEST_FAIL;

  float minDist = 1e9f, maxDist = 0.0f;
  for (int i = 0; i < data.vertCount; i++) {
    float d = Vector3Distance(data.verts[i], center);
    if (d < minDist) minDist = d;
    if (d > maxDist) maxDist = d;
  }
  float lo = radius * (1.0f - jitter) - 0.5f;
  float hi = radius * (1.0f + jitter) + 0.5f;
  if (!AutoTest_ExpectTrue(minDist >= lo && maxDist <= hi,
                            "every rock vertex should stay within radius*(1 +/- jitter)", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectTrue((maxDist - minDist) > 2.0f,
                            "jitter should actually vary vertex distance, not collapse to a perfect sphere", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralShardClusterStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 origin = {0, 0, 0};
  Vector3 mainDir = {0, 1, 0};
  int shardCount = 8;
  float minLength = 20.0f, maxLength = 60.0f;
  ShardClusterMeshData data;
  ProceduralMesh_BuildShardCluster(&data, origin, mainDir, shardCount, minLength, maxLength, 7, NULL);
  ShardClusterConfig cfg = ProceduralMesh_DefaultShardClusterConfig();
  Vector3 dirN = Vector3Normalize(mainDir);

  for (int s = 0; s < data.shardCount; s++) {
    if (!AutoTest_ExpectFloatNear(Vector3Distance(data.baseCenter[s], origin), 0.0f, 0.1f,
                                   "every shard should originate exactly at origin", outReason, outReasonSize))
      return AUTOTEST_FAIL;

    float length = Vector3Distance(data.tipCenter[s], data.baseCenter[s]);
    if (!AutoTest_ExpectTrue(length >= minLength - 0.5f && length <= maxLength + 0.5f,
                              "shard length should stay within [minLength, maxLength]", outReason, outReasonSize))
      return AUTOTEST_FAIL;

    Vector3 shardDir = Vector3Normalize(Vector3Subtract(data.tipCenter[s], data.baseCenter[s]));
    float cosAngle = Vector3DotProduct(shardDir, dirN);
    float cosLimit = cosf(cfg.spreadAngle) - 0.01f;
    if (!AutoTest_ExpectTrue(cosAngle >= cosLimit,
                              "shard direction should stay within spreadAngle of mainDirection", outReason, outReasonSize))
      return AUTOTEST_FAIL;
  }
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralVortexFunnelStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 center = {0, 0, 0};
  // radialSegs=20 chosen so ridgeCount(5) mod radialSegs != 0 -- the ridge
  // cosine term then sums to exactly 0 over a full ring (root-of-unity
  // identity), letting the average radius check below be exact rather than
  // needing to model the ridge wave itself.
  int heightSegs = 10, radialSegs = 20;
  VortexFunnelMeshData data;
  ProceduralMesh_BuildVortexFunnel(&data, center, NULL, heightSegs, radialSegs, 0.0f);
  VortexFunnelConfig cfg = ProceduralMesh_DefaultVortexFunnelConfig();

  float sumBottom = 0.0f, sumTop = 0.0f;
  for (int j = 0; j < radialSegs; j++) {
    Vector3 b = data.rings[0][j];
    Vector3 t = data.rings[heightSegs][j];
    sumBottom += sqrtf((b.x - center.x) * (b.x - center.x) + (b.z - center.z) * (b.z - center.z));
    sumTop += sqrtf((t.x - center.x) * (t.x - center.x) + (t.z - center.z) * (t.z - center.z));

    if (!AutoTest_ExpectFloatNear(b.y, center.y, 0.1f, "bottom ring should sit at y=0", outReason, outReasonSize))
      return AUTOTEST_FAIL;
    if (!AutoTest_ExpectFloatNear(t.y, center.y + cfg.height, 0.1f, "top ring should sit at y=height", outReason, outReasonSize))
      return AUTOTEST_FAIL;
  }
  float avgBottom = sumBottom / (float)radialSegs;
  float avgTop = sumTop / (float)radialSegs;
  if (!AutoTest_ExpectFloatNear(avgBottom, cfg.bottomRadius, 1.0f,
                                 "bottom ring average radius should match bottomRadius", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  if (!AutoTest_ExpectFloatNear(avgTop, cfg.topRadius, 1.0f,
                                 "top ring average radius should match topRadius", outReason, outReasonSize))
    return AUTOTEST_FAIL;
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralFissureStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  Vector3 pathPoints[2] = {{0, 0, 0}, {300, 0, 0}};
  float width = 40.0f, depth = 15.0f, jaggedness = 0.3f;
  FissureMeshData data;
  ProceduralMesh_BuildFissure(&data, pathPoints, 2, width, depth, jaggedness, 99);

  if (!AutoTest_ExpectTrue(data.segments > 0, "fissure should produce at least one segment along the path",
                            outReason, outReasonSize))
    return AUTOTEST_FAIL;

  for (int i = 0; i <= data.segments; i++) {
    float edgeL_y = data.verts[i][0].y;
    float bottom_y = data.verts[i][2].y;
    float edgeR_y = data.verts[i][4].y;
    if (!AutoTest_ExpectTrue(bottom_y < edgeL_y && bottom_y < edgeR_y,
                              "cross-section bottom vertex should be the deepest point (V-shape)", outReason, outReasonSize))
      return AUTOTEST_FAIL;
    if (!AutoTest_ExpectFloatNear(bottom_y, -depth, depth * 0.5f,
                                   "trench bottom should sit close to -depth", outReason, outReasonSize))
      return AUTOTEST_FAIL;

    float span = Vector3Distance(data.verts[i][0], data.verts[i][4]);
    if (!AutoTest_ExpectFloatNear(span, width, width * 0.3f,
                                   "fissure edge-to-edge span should be close to width", outReason, outReasonSize))
      return AUTOTEST_FAIL;
  }
  return AUTOTEST_PASS;
}

static AutoTestResult CoreTestSkill_ProceduralBaseMeshStep(int frameInCase, char *outReason, int outReasonSize) {
  if (frameInCase != 0) return AUTOTEST_RUNNING;

  float width = 100.0f, length = 80.0f;
  int segX = 4, segZ = 3;
  Mesh grid = ProceduralMesh_CreateBaseGrid(width, length, segX, segZ);

  int expectedVerts = (segX + 1) * (segZ + 1);
  int expectedTris = segX * segZ * 2;
  bool ok = AutoTest_ExpectTrue(grid.vertexCount == expectedVerts && grid.triangleCount == expectedTris,
                                 "base grid vertex/triangle count should match segmentsX/Z", outReason, outReasonSize);
  if (ok) {
    int lastIdx = expectedVerts - 1;
    float x0 = grid.vertices[0], xLast = grid.vertices[lastIdx * 3 + 0], zLast = grid.vertices[lastIdx * 3 + 2];
    ok = AutoTest_ExpectFloatNear(x0, -width * 0.5f, 0.1f, "grid vertex 0 should sit at -width/2", outReason, outReasonSize)
      && AutoTest_ExpectFloatNear(xLast, width * 0.5f, 0.1f, "grid last vertex should sit at +width/2", outReason, outReasonSize)
      && AutoTest_ExpectFloatNear(zLast, length * 0.5f, 0.1f, "grid last vertex should sit at +length/2", outReason, outReasonSize);
  }
  ProceduralMesh_UnloadBase(&grid);
  if (!ok) return AUTOTEST_FAIL;

  int radialSegs = 8, heightSegs = 4;
  Mesh cyl = ProceduralMesh_CreateBaseCylinder(radialSegs, heightSegs);
  int expectedCylVerts = (radialSegs + 1) * (heightSegs + 1);
  int expectedCylTris = radialSegs * heightSegs * 2;
  ok = AutoTest_ExpectTrue(cyl.vertexCount == expectedCylVerts && cyl.triangleCount == expectedCylTris,
                            "base cylinder vertex/triangle count should match radialSegs/heightSegs", outReason, outReasonSize);
  if (ok) {
    int idx = (heightSegs / 2) * (radialSegs + 1) + 1;
    float x = cyl.vertices[idx * 3 + 0];
    float y = cyl.vertices[idx * 3 + 1];
    float z = cyl.vertices[idx * 3 + 2];
    float r = sqrtf(x * x + z * z);
    ok = AutoTest_ExpectFloatNear(r, 1.0f, 0.05f, "base cylinder vertices should sit at unit local radius", outReason, outReasonSize)
      && AutoTest_ExpectTrue(y >= 0.0f && y <= 1.0f, "base cylinder local height should stay within [0,1]", outReason, outReasonSize);
  }
  ProceduralMesh_UnloadBase(&cyl);
  return ok ? AUTOTEST_PASS : AUTOTEST_FAIL;
}

void InitCoreTestSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  s_softShader = ResourceManager_LoadShader(
      "skills/taiji/core_test/core_test_soft.vs",
      "skills/taiji/core_test/core_test_soft.fs");
  s_fadeDistLoc = GetShaderLocation(s_softShader, "u_fadeDistance");
  s_debugShowFadeLoc = GetShaderLocation(s_softShader, "u_debugShowFade");
  s_lightDirLoc = GetShaderLocation(s_softShader, "u_lightDir");
  s_shaderLoaded = (s_softShader.id != 0);

  s_coreTestSkillIndex = Skill_GetIndexByName("CORE_TEST");

  if (AutoTest_IsEnabled()) {
      AutoTest_Register("soft_particle_ground_fade", CoreTestSkill_AutoTestStep, 10);
      AutoTest_Register("vfx_light_priority_eviction", CoreTestSkill_VFXLightEvictionStep, 5);
      AutoTest_Register("trail_priority_eviction", CoreTestSkill_TrailEvictionStep, 5);
      AutoTest_Register("skill_cooldown_gating", CoreTestSkill_CooldownGatingStep, 5);
      AutoTest_Register("skill_abort_registration", CoreTestSkill_AbortRegistrationStep, 5);
      AutoTest_Register("skill_lifecycle_query", CoreTestSkill_LifecycleQueryStep, 5);
      AutoTest_Register("debug_draw_pixel_check", CoreTestSkill_DebugDrawStep, 10);
      AutoTest_Register("skill_smoke_fire", SkillSmokeTest_Fire, 10);
      AutoTest_Register("skill_smoke_tube", SkillSmokeTest_Tube, 10);
      AutoTest_Register("skill_smoke_water_sphere", SkillSmokeTest_WaterSphere, 10);
      AutoTest_Register("skill_smoke_wood_thorns", SkillSmokeTest_WoodThorns, 10);
      AutoTest_Register("skill_smoke_stone_prison", SkillSmokeTest_StonePrison, 10);
      AutoTest_Register("skill_smoke_hoa_long_phong_ba", SkillSmokeTest_HoaLongPhongBa, 10);
      AutoTest_Register("procedural_mesh_tube", CoreTestSkill_ProceduralTubeStep, 5);
      AutoTest_Register("procedural_mesh_wave_plane", CoreTestSkill_ProceduralWavePlaneStep, 5);
      AutoTest_Register("procedural_mesh_curling_wave", CoreTestSkill_ProceduralCurlingWaveStep, 5);
      AutoTest_Register("procedural_mesh_rock", CoreTestSkill_ProceduralRockStep, 5);
      AutoTest_Register("procedural_mesh_shard_cluster", CoreTestSkill_ProceduralShardClusterStep, 5);
      AutoTest_Register("procedural_mesh_vortex_funnel", CoreTestSkill_ProceduralVortexFunnelStep, 5);
      AutoTest_Register("procedural_mesh_fissure", CoreTestSkill_ProceduralFissureStep, 5);
      AutoTest_Register("procedural_mesh_base_grid_cylinder", CoreTestSkill_ProceduralBaseMeshStep, 5);
  }
}

// Sphere center at ground level (Y=0) — half buried, half exposed, in an
// open area with no occluder in front (per Item 3's open question). Shared
// by the real Cast path and the autotest bridge below.
void CoreTestSkill_ForceActivate(int agentId, Vector3 spherePos) {
  s_spherePos = spherePos;
  s_testActive = true;
  s_showSoftParticleSphere = true;
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
  (void)params;
  (void)startPos;
  // Interactive cast: Spawns the Soft Particle Sphere at the clicked target position
  // at ground level (Y=0) so that the user can visually inspect the soft fade on screen.
  s_spherePos = target;
  s_spherePos.y = 0.0f;
  s_testActive = true;
  s_showSoftParticleSphere = true;
  s_ownerAgentId = agentId;

  // Reset debug readback flag for a new cast
  s_hasReadback = false;
}

void UpdateCoreTestSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;
  if (s_testActive) {
    if (IsKeyPressed(KEY_L)) {
      CoreTestSkill_TriggerReadback();
    }
    if (IsKeyPressed(KEY_H)) {
      s_debugMode = (s_debugMode + 1) % 3;
    }
    // Oscillation to show the soft intersection dynamically
    static float time = 0.0f;
    time += dt;
    s_spherePos.y = sinf(time * 2.0f) * s_sphereRadius * 0.8f;
  }
}

void DrawCoreTestSkill(void) {
  if (s_debugDrawTestActive) {
    DebugDraw_Sphere(s_debugDrawTestPos, CORE_TEST_DEBUG_DRAW_RADIUS, CORE_TEST_DEBUG_DRAW_COLOR);
  }

  if (!s_testActive || !s_shaderLoaded || !s_showSoftParticleSphere) return;

  BeginShaderMode(s_softShader);
  SkillManager_BeginShader(s_softShader);
  if (s_fadeDistLoc >= 0) {
    float fade = CORE_TEST_SOFT_FADE_DISTANCE;
    SetShaderValue(s_softShader, s_fadeDistLoc, &fade, SHADER_UNIFORM_FLOAT);
  }
  if (s_lightDirLoc >= 0) {
    Vector3 lightDir = {0.0f, 1.0f, 0.0f}; // Default pointing up towards light
    SetShaderValue(s_softShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
  }
  int matModelLoc = GetShaderLocation(s_softShader, "matModel");
  if (matModelLoc >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_softShader, matModelLoc, identity);
  }
  if (s_debugShowFadeLoc >= 0) {
    float debugFlag = (float)s_debugMode;
    SetShaderValue(s_softShader, s_debugShowFadeLoc, &debugFlag, SHADER_UNIFORM_FLOAT);
  }
  ScreenDistort_BindDepthForSoftParticles(s_softShader, 3);

  BeginBlendMode(BLEND_ALPHA);
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  rlDisableDepthMask();
  DrawCoreSphere(s_spherePos, s_sphereRadius, 24, 24, ELEMENT_COLOR_TAIJI);
  rlDrawRenderBatchActive();
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

  if (!s_showSoftParticleSphere) {
    DrawRectangle(x - 6, y - 4, 450, 30, ColorAlpha(BLACK, 0.55f));
    DrawText("CORE_TEST - Click ground: spawn soft sphere at target", x, y, 16, YELLOW);
    return;
  }

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
