#include "skills/water/water_stream/tube_skill.h"
#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decals/decal_system.h"
#include "core/force_field.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include "core/particles/particle_system.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/utils_math.h"
#include "core/vfx_light.h"
#include "combat/combat.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Pool-size constant — sizes a static array, not tunable.
#define MAX_TUBE_EMITTERS 5

// Visual mesh constants — not spatial dimensions, unitless or UV.
#define TUBE_SEGMENTS         30
#define TUBE_RADIAL_SEGMENTS  20
#define TUBE_UV_LENGTH_SCALE   3.0f

// TUBE_TRAVEL_SPEED: unitless Bezier progress per second (0..1), NOT a
// spatial speed.  Keep as a constant since it governs animation pacing.
#define TUBE_TRAVEL_SPEED      1.2f

#include "tube_skill_params.inl"



typedef struct {
  bool active;
  int ownerAgentId;
  Vector3 p0, p1, p2, p3;
  float progress;
  float sizeScale;
  Vector3 headPos;
} TubeEmitter;

static TubeEmitter emitters[MAX_TUBE_EMITTERS];
static int s_skillIndex = -1;

static TubeMeshConfig s_waterTubeConfig;
// F0 purge: ImpactBurstConfig and VFX_TriggerImpactBurst are deleted. The
// successor takes no config struct — the E6 package IS the tuning, and
// `severity01` is the only dial it exposes on purpose.
#define TUBE_IMPACT_SEVERITY 0.40f   // under the 0.45 hitstop gate on purpose

static inline float ClampSizeScale(float scale) {
  return Clamp(scale, 0.2f, 3.0f);
}

static Texture2D s_causticsTex;
static ColorGradient s_splashGrad;
static ForceField s_tubeSplashField;  // used by impact burst (fixed layers)
static ForceField s_tubeMistField;    // rebuilt each frame

// Rebuild the static splash field (impact burst uses this — fixed layers,
// rebuilt when gravity/noise tunables change).
static void RebuildSplashField(void) {
    ForceField_Clear(&s_tubeSplashField);
    ForceField_AddLayer(&s_tubeSplashField, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1.0f, 0},
        .strength  = s_splashGravity
    });
    ForceField_AddLayer(&s_tubeSplashField, (ForceLayer){
        .type       = FORCE_NOISE_PERLIN,
        .strength   = s_splashNoise,
        .noiseScale = 0.010f,
        .noiseSpeed = 0.5f
    });
    ForceField_AddLayer(&s_tubeSplashField,
        (ForceLayer){.type = FORCE_DRAG, .strength = s_splashDrag});
}

// Rebuild the mist force field from tunables before emitting mist particles
// so sandbox changes take effect immediately.
static void RebuildMistField(void) {
    ForceField_Clear(&s_mistFieldActive);
    ForceField_AddLayer(&s_mistFieldActive, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1.0f, 0},
        .strength  = s_mistGravity
    });
    ForceField_AddLayer(&s_mistFieldActive, (ForceLayer){
        .type       = FORCE_NOISE_PERLIN,
        .strength   = s_mistNoise,
        .noiseScale = 0.008f,
        .noiseSpeed = 0.3f
    });
    ForceField_AddLayer(&s_mistFieldActive,
        (ForceLayer){.type = FORCE_DRAG, .strength = s_mistDrag});
    SkillForceMix_AddLayers(&s_mistForce, &s_mistFieldActive);
}

// F0 purge: the ImpactBurstConfig this used to fill is deleted along with
// VFX_TriggerImpactBurst. The successor, VFX_ComposeImpactPackage, deliberately
// exposes ONE dial (severity) instead of a 20-field struct — the whole point of
// E6 #6 was that the pieces are tuned together, so a per-skill config would put
// back exactly what it removed. The tunables below still exist and still
// hot-reload; they simply have nothing to drive until a skill needs its own
// variant, which is an E7 question.
static void RebuildImpactConfig(void) {}

void InitTubeSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;

  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    emitters[i].active = false;
  }

  s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");

  ColorGradient_StandardFade(&s_splashGrad, ELEMENT_COLOR_WATER, 0.40f, 0.2f);

  // Seed per-phase curves flat (no-op until shaped in sandbox)
  SkillCurve_SetConstant(&s_mistRadiusCurve,   1.0f);
  SkillCurve_SetConstant(&s_mistSpeedCurve,    1.0f);
  SkillCurve_SetConstant(&s_mistAlphaCurve,    1.0f);
  SkillCurve_SetConstant(&s_mistEmissiveCurve, 1.0f);

  s_waterTubeConfig = ProceduralMesh_DefaultTubeConfig();

  RebuildSplashField();
  // s_tubeMistField is kept for backward compat (not used in Update — we use
  // s_mistFieldActive which is rebuilt each frame from tunables).
  ForceField_Clear(&s_tubeMistField);

  RebuildImpactConfig();

  s_skillIndex = Skill_GetIndexByName("TUBE");

  static SkillTunableEntry s_tunables[TUBE_TUNABLE_COUNT];
  int tn = 0;
  #include "tube_skill_tunables.inl"

  SkillTunables_LoadPersisted(
      "skills/water/water_stream/tube_skill.tuning",
      s_tunables, tn);
  RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}

void CastTubeSkill(int agentId, Vector3 startPos, Vector3 target, float twistPhase,
                   float sizeScale) {
  if (!SkillManager_CanCast(s_skillIndex, agentId))
    return;

  float clampedScale = ClampSizeScale(sizeScale);
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active       = true;
      emitters[i].ownerAgentId = agentId;
      emitters[i].p0           = startPos;
      emitters[i].p3           = target;
      emitters[i].progress     = 0.0f;
      emitters[i].sizeScale    = clampedScale;
      emitters[i].headPos      = startPos;

      float dist   = Vector3Distance(startPos, target);
      Vector3 dir  = Vector3Normalize(Vector3Subtract(target, startPos));
      Vector3 up   = (Vector3){0.0f, 1.0f, 0.0f};
      Vector3 right = Vector3Normalize(Vector3CrossProduct(up, dir));

      // Distance-proportional lateral/height offsets — floor+cap (Pass 4)
      float lateralOffset = fminf(fmaxf(dist * 0.4f * cosf(twistPhase), 0.2f), dist * 0.6f);
      float heightOffset  = fminf(fmaxf(dist * 0.3f, 0.1f), dist * 0.5f);

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.3f));
      emitters[i].p1 = Vector3Add(emitters[i].p1, Vector3Scale(right, lateralOffset));
      emitters[i].p1.y += heightOffset;

      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.7f));
      emitters[i].p2 = Vector3Add(emitters[i].p2, Vector3Scale(right, -lateralOffset));
      emitters[i].p2.y += heightOffset * 0.5f;
      break;
    }
  }

  SkillParams cdParams = {0};
  cdParams.sizeScale = clampedScale;
  SkillManager_TriggerCooldown(s_skillIndex, agentId,
                               Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, cdParams));
}

void UpdateTubeSkill(float dt) {
  // Zero-instance early-out: skip per-frame field rebuilds when idle.
  bool anyActive = false;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active) { anyActive = true; break; }
  }
  if (!anyActive)
    return;

  RebuildSplashField();
  RebuildImpactConfig();

  // Đấu Pháp events (peek, ids = skillIndex*1000 + slot): a lost clash or
  // an agent hit (combat already applied the damage) bursts the stream at
  // the clash point instead of its Bezier endpoint.
  {
    const ClashEvent *ev;
    int evCount = Combat_PeekEvents(&ev);
    for (int k = 0; k < evCount; k++) {
      int slot = ev[k].skillInstanceId - s_skillIndex * 1000;
      if (slot < 0 || slot >= MAX_TUBE_EMITTERS || !emitters[slot].active) continue;
      if (ev[k].outcome == CLASH_B_WINS || ev[k].outcome == CLASH_MUTUAL_DESTROY ||
          ev[k].outcome == CLASH_HIT_AGENT) {
        emitters[slot].active = false;
        VFX_ComposeImpactPackage(ev[k].clashPoint, (Vector3){0.0f, 1.0f, 0.0f},
                                 VC_MAT_WATER, emitters[slot].sizeScale, TUBE_IMPACT_SEVERITY);
      }
    }
  }

  for (int e = 0; e < MAX_TUBE_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    emitters[e].progress += dt * TUBE_TRAVEL_SPEED;
    if (emitters[e].progress >= 1.0f) {
      emitters[e].active = false;
      VFX_ComposeImpactPackage(emitters[e].p3, (Vector3){0.0f, 1.0f, 0.0f},
                                 VC_MAT_WATER, emitters[e].sizeScale, TUBE_IMPACT_SEVERITY);
      continue;
    }
    emitters[e].headPos = ProceduralMesh_BezierPoint(
        emitters[e].p0, emitters[e].p1, emitters[e].p2, emitters[e].p3,
        emitters[e].progress);

    // Đấu Pháp: submit the stream head as a combat collider — combat owns
    // hit detection + agent damage (COMBAT_API.md §5).
    Combat_SubmitProjectile(emitters[e].ownerAgentId, ELEM_WATER,
                            emitters[e].headPos,
                            0.2f * emitters[e].sizeScale,
                            8.0f * emitters[e].sizeScale,
                            2.0f,
                            s_skillIndex * 1000 + e);

    if (GetRandomValue(0, 100) < 60) {
      RebuildMistField();
      ParticleConfig cfgMist = {0};
      cfgMist.position   = emitters[e].headPos;
      cfgMist.velocity   = (Vector3){
          (Random01() - 0.5f) * s_mistVelXZ * 2.0f,
          Random01() * s_mistVelYMax,
          (Random01() - 0.5f) * s_mistVelXZ * 2.0f
      };
      cfgMist.radius     = Math_Mix(s_mistRadiusMin, s_mistRadiusMax, Random01())
                           * emitters[e].sizeScale;
      cfgMist.lifetime   = Math_Mix(s_mistLifeMin, s_mistLifeMax, Random01());
      cfgMist.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.7f);
      cfgMist.colorEnd   = (Color){255, 255, 255, 0}; // lint: allow-color
      cfgMist.forceField = &s_mistFieldActive;
      cfgMist.gradient   = &s_splashGrad;
      cfgMist.radiusCurve   = &s_mistRadiusCurve;
      cfgMist.speedCurve    = &s_mistSpeedCurve;
      cfgMist.alphaCurve    = &s_mistAlphaCurve;
      cfgMist.emissiveCurve = &s_mistEmissiveCurve;
      // THE BLEND LAW (core/particle_system.h): this mist EMITS — it is the
      // stream's own glow coming apart — so it draws additive and unlit. Left
      // alpha-blended it goes through the lighting multiply, and in the night
      // arena that renders it black.
      cfgMist.render.blendMode = VFX_BLEND_ADDITIVE;
      cfgMist.render.unlit = 1;
      cfgMist.render.emissiveBoost = 1.15f;
      SpawnParticle(cfgMist);
    }
  }
}

void DrawTubeSkill(void) {
  bool anyActive = false;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active) { anyActive = true; break; }
  }
  if (!anyActive) return;

  float time = GetTime();

  // ONE Begin/End around the whole loop, not per emitter. Each pair binds the
  // tube shader and sets its uniforms; doing that per stream flushes the batch
  // for every projectile on screen, which is the difference between one draw
  // state change and MAX_TUBE_EMITTERS of them.
  VFX_BeginWaterStreams(time);
  for (int e = 0; e < MAX_TUBE_EMITTERS; e++) {
    if (!emitters[e].active) continue;
    float radius = s_tubeBaseRadius * emitters[e].sizeScale;
    // The stream body itself: a tube mesh swept along the emitter's own Bezier,
    // revealed up to `progress`, so the head is where the collider is.
    VFX_ComposeWaterStream(emitters[e].p0, emitters[e].p1, emitters[e].p2,
                           emitters[e].p3, radius, emitters[e].progress, time);
  }
  VFX_EndWaterStreams();

}

void UnloadTubeSkill(void) {
  /* Assets are cached and managed globally by the Resource Manager */
}

int GetTubeSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      outProjectiles[count].position = emitters[i].headPos;
      outProjectiles[count].radius   = s_tubeBaseRadius * emitters[i].sizeScale * 1.6f;
      outProjectiles[count].active   = true;
      count++;
    }
  }
  return count;
}

void DeactivateTubeProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_TUBE_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        VFX_ComposeImpactPackage(emitters[i].headPos, (Vector3){0.0f, 1.0f, 0.0f},
                                 VC_MAT_WATER, emitters[i].sizeScale, TUBE_IMPACT_SEVERITY);
        return;
      }
      count++;
    }
  }
}
