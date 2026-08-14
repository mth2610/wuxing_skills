#include "particle_system.h"
#include "core/particles/particle_manager.h"
#include "core/mesh_adjacency.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/utils_math.h"
#include "core/ribbon_strip.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "core/gfx_quality.h"
#include "core/tuning.h"
#include "core/screen_distort.h"
#include "environment/environment_system.h"
#include <string.h>
#include <math.h>

#define MAX_PARTICLES 2000

// Trail ribbon buffer: 8 recorded history points, optionally subdivided.
#define PS_TRAIL_SUBDIV         4
#define PS_TRAIL_MAX_RIBBON_PTS 32

// Catmull-Rom through p1..p2 (p0/p3 are the neighbouring points that set the
// tangents). Chosen over a Bezier because it INTERPOLATES the recorded points
// — a smoothed trail must still pass through where the particle actually was.
static inline Vector3 PS_CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
  float t2 = t * t, t3 = t2 * t;
  Vector3 r;
  r.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
  r.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
  r.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t +
                (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
                (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
  return r;
}

// TỐI ƯU 1: Sắp xếp lại thứ tự biến (Data Alignment & Hot/Cold Split)
// Các biến hay dùng (Physics) đặt lên đầu để vừa khít 1 CPU Cache Line (64 bytes)
typedef struct
{
  // --- HOT DATA (Dùng mỗi frame) ---
  float x, y, z; // Tách Vector3 thành scalar để inline math
  float vx, vy, vz;
  float lifetime;
  float maxLifetime;

  float rotation;
  float angularVelocity;

  // --- WARM DATA (Dùng lúc vẽ) ---
  Color colorStart;
  Color colorEnd;
  float radius;
  bool active;
  int emitterId;
  int renderMode;

  // --- COLD DATA (Con trỏ, ít khi rẽ nhánh) ---
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
  float spriteAnimPhase;
  float spriteAnimRate;
  bool spriteFlipX;
  bool spriteFlipY;
  const Vector3 *followTarget;
  const unsigned int *followTargetGeneration;
  unsigned int followGeneration;
  Vector3 followTargetLast;
  float followStrength;
  const SkillCurve *followCurve;
  unsigned int texId;   // 0 = use the batch default passed to DrawParticles
  int blendMode;        // VFX_BlendMode — see the blend law in vfx_config.h
  int unlit;            // 1 = emissive, skip the lighting multiply
  float emissiveBoost;  // >1 = HDR headroom for a glowing core (see vfx_config.h)
  int volumeSheet;      // 1 = texture is a packed 4-channel volume flipbook
  unsigned int rampTexId; // black-body/magic LUT for volumeSheet; 0 = none
  float heatGain;       // exposure on the sheet's emission before the ramp
  float smokeGain;      // multiplier on the sheet's soot density
  Color smokeTint;      // colour of the soot half (black/white smoke)
  VFXContrastProfileId contrastProfile;
  const SkillCurve *radiusCurve;
  const SkillCurve *speedCurve;
  const SkillCurve *alphaCurve;
  const SkillCurve *emissiveCurve;

  // CẢNH BÁO: Việc lưu nguyên ParticleConfig ở đây vẫn tốn bộ nhớ.
  // Lý tưởng nhất sau này bạn nên đổi thành con trỏ tới 1 "SubEmitterPool"
  ParticleConfig onDeathConfig;
  int onDeathCount;
  bool hasDeathEmit;

  ParticleConfig onLiveConfig;
  float onLiveEmitRate;
  float onLiveEmitTimer;
  bool hasLiveEmit;

  // Stretch rendering
  float stretchStrength;
  float stretchMinSpeed;

  // Physical collision
  bool collisionEnabled;
  float collisionElasticity;
  float collisionFloorY;
  ParticleConfig onCollisionConfig;
  int onCollisionCount;
  bool hasCollisionEmit;

  // Particle trails history (static buffer, no malloc!)
  int trailLength;
  float trailWidthRatio;
  Color trailColorStart;
  Color trailColorEnd;
  float trailStepTime;
  int trailOnly;
  int trailSmooth;
  Vector3 trailHistory[8];
  unsigned char trailHistoryCount;
  float trailHistoryTimer;
} ParticleInternal;

static ParticleInternal g_Particles[MAX_PARTICLES];
static float s_particleTime = 0.0f;

static int s_freeHead = 0;
static int s_nextFree[MAX_PARTICLES];
static int s_activeIds[MAX_PARTICLES];
static int s_activeCount = 0;
static int s_slotListIndex[MAX_PARTICLES];

static inline void Particle_Deactivate(int idx)
{
  g_Particles[idx].active = false;
  int listIdx = s_slotListIndex[idx];
  int lastId = s_activeIds[s_activeCount - 1];

  s_activeIds[listIdx] = lastId;
  s_slotListIndex[lastId] = listIdx;
  s_activeCount--;
  s_slotListIndex[idx] = -1;

  s_nextFree[idx] = s_freeHead;
  s_freeHead = idx;
}

static inline int Particle_AllocSlot(void)
{
  if (s_freeHead >= MAX_PARTICLES)
    return -1;
  int idx = s_freeHead;
  s_freeHead = s_nextFree[idx];

  s_slotListIndex[idx] = s_activeCount;
  s_activeIds[s_activeCount] = idx;
  s_activeCount++;
  return idx;
}

// Đợt E / F1 — lit particles. Declared up here (rather than beside the draw
// code that uses them) so InitParticleSystem can register them as tunables.
// Both default to 0 = the exact pre-F1 unlit look.
// Tuned 22/07/2026 against the smoke puff. Lighting stays OFF by default so
// nothing already shipped changes look; the rest are the values that made smoke
// read as smoke, promoted out of tuning.cfg so a fresh clone gets them.
static float s_lightingStrength = 0.0f;
static float s_scatterStrength  = 0.0f;  // shape-neutral: raise only for backlit glow
static float s_debugNormal      = 0.0f;  // 1 = paint particle normals as RGB
static float s_normalBulge      = 1.0f;  // 1 = true hemisphere; >1 exaggerates
static float s_sunGain          = 1.0f;  // night sun/ambient are ~0.2 — without
static float s_ambientGain      = 0.25f; // gain, lighting just dims the smoke
static float s_lightAzimuth     = -1.0f; // <0 = real sun; >=0 = debug override
static float s_analyticUV       = 1.0f;  // 0 only if a SpriteAnim atlas is in use
// SOFT PARTICLES. A billboard that intersects the ground is CUT by the depth
// test along a dead-straight line, and the bigger the sprite the more it reads
// as a sheet of paper pushed into the floor. The fade hides the cut by taking
// the alpha to zero just before the geometry gets there.
//
// Metres. Too small and the cut is still visible; too large and sprites go
// translucent while nowhere near anything, which reads as the effect dimming
// for no reason. Sized against the sprites that show it worst — the 0.2-1.0 m
// smoke billows.
// rlvk now resolves texture0 by reflected sampler name, so its second depth
// sampler no longer displaces the sprite texture (HANDOFF §7.30).
static float s_softFade         = 0.35f;
static float s_softDebug        = 0.0f;
static int   s_locSoftFade      = -1;
static int   s_locSoftDebug     = -1;
static bool  s_softBound        = false;
#define PARTICLE_SOFT_DEPTH_SLOT 3
static int   s_locAtlasGrid     = -1;
static int   s_locEmissiveBoost = -1;
// How far above 1.0 an EMISSIVE particle writes into the HDR scene buffer. 1.0
// reproduces the old look exactly (no core, no bloom); ~3 gives the AAA
// white-hot core with a coloured rim. Only applied to unlit/additive batches.
static float s_emissiveBoost    = 1.0f;  // GLOBAL multiplier; per-particle value carries the intent

void InitParticleSystem(void)
{
  for (int i = 0; i < MAX_PARTICLES - 1; i++)
    s_nextFree[i] = i + 1;
  s_nextFree[MAX_PARTICLES - 1] = MAX_PARTICLES;
  s_freeHead = 0;
  s_activeCount = 0;
  for (int i = 0; i < MAX_PARTICLES; i++)
  {
    g_Particles[i].active = false;
    s_slotListIndex[i] = -1;
  }

}

void SpawnParticle(ParticleConfig config)
{
  /* Phase-A compatibility wrapper: legacy call sites become AUTO one-shot
   * emitters without exposing a backend selection to their authors. */
  ParticleManager_SpawnCompatibility(config);
}

void ParticleSystem_SpawnLegacy(ParticleConfig config)
{
  ParticleSystem_SpawnFromEmitter(config, -1, 0);
}

void ParticleSystem_SpawnFromEmitter(ParticleConfig config, int emitterId, int renderMode)
{
  ParticleConfig_Unify(&config);
  int targetIdx = Particle_AllocSlot();
  if (targetIdx == -1)
    return;

  ParticleInternal *p = &g_Particles[targetIdx];
  p->x = config.position.x;
  p->y = config.position.y;
  p->z = config.position.z;
  p->vx = config.velocity.x;
  p->vy = config.velocity.y;
  p->vz = config.velocity.z;

  p->colorStart = config.colorStart;
  p->colorEnd = config.colorEnd;
  p->radius = config.radius;
  p->lifetime = config.lifetime;
  p->maxLifetime = config.lifetime;
  p->emitterId = emitterId;
  p->renderMode = renderMode;

  p->forceField = config.forceField;
  p->gradient = config.gradient;
  p->spriteAnim = config.spriteAnim;
  p->spriteAnimPhase = config.spriteAnimPhase;
  p->spriteAnimRate = config.spriteAnimRate > 0.0f ? config.spriteAnimRate : 1.0f;
  p->spriteFlipX = config.spriteFlipX;
  p->spriteFlipY = config.spriteFlipY;
  p->followTarget = config.followTarget;
  p->followTargetGeneration = config.followTargetGeneration;
  p->followGeneration = config.followGeneration;
  p->followTargetLast = config.followTarget ? *config.followTarget : (Vector3){0};
  p->followStrength = config.followStrength;
  p->followCurve = config.followCurve;
  p->texId = config.render.texture.id;
  VFXResolvedAppearance particleAppearance = VFXAppearance_Resolve(
      config.render.appearance,
      (VFXResolvedAppearance){
          .surface = (VFXSurfaceMode)config.render.blendMode,
          .contrast = config.render.contrastProfile,
          .bodyOpacity = config.render.blendMode == VFX_BLEND_ALPHA ? 1.0f : 0.0f,
          .emissionIntensity = config.render.emissiveBoost > 0.0f
                                   ? config.render.emissiveBoost : 1.0f,
          .emissionThreshold = 1.0f,
          .unlit = config.render.unlit != 0
      });
  // Only the packed-volume shader produces mathematically premultiplied RGB.
  // A regular sprite still gets the FIRE look in one alpha/HDR draw, without
  // feeding straight RGB into a premultiplied blend law.
  if (config.render.appearance == VFX_APPEARANCE_FIRE &&
      !config.render.volumeSheet)
    particleAppearance.surface = VFX_SURFACE_ALPHA;
  p->blendMode = (int)particleAppearance.surface;
  p->unlit = particleAppearance.unlit ? 1 : 0;
  p->contrastProfile = particleAppearance.contrast;
  // Particle shaders use this as a colour gain even for non-emissive alpha
  // particles, so semantic emission 0 maps to neutral gain 1 here.
  p->emissiveBoost = particleAppearance.emissionIntensity > 0.0f
                         ? particleAppearance.emissionIntensity : 1.0f;
  p->volumeSheet = config.render.volumeSheet;
  p->rampTexId = config.render.rampLUT.id;
  p->heatGain = (config.render.heatGain > 0.0f) ? config.render.heatGain : 1.0f;
  p->smokeGain = (config.render.smokeGain > 0.0f) ? config.render.smokeGain : 1.0f;
  // {0,0,0,0} means "unset" — fall back to soot rather than to black, which
  // would silently delete the smoke half of every volume sheet.
  p->smokeTint = (config.render.smokeTint.a != 0) ? config.render.smokeTint
                                                  : (Color){82, 74, 69, 255};
  // A volume sheet with no ramp would index an unbound sampler, which reads
  // black — the flame would vanish while everything else said it was drawing.
  // Fall back to the legacy path instead: the sprite looks wrong (its RGB is
  // three density channels, not a colour) but it is VISIBLE and the warning
  // says why, which is the difference between a bug you can see and one you
  // spend an evening on.
  if (p->volumeSheet && p->rampTexId == 0)
  {
    static bool warned = false;
    if (!warned)
    {
      warned = true;
      TraceLog(LOG_WARNING, "PARTICLE: volumeSheet particle spawned with no "
                            "rampLUT — bake one with ColorGradient_BakeLUT. "
                            "Falling back to the legacy colour path.");
    }
    p->volumeSheet = 0;
  }
  if (p->blendMode == VFX_BLEND_ADDITIVE)
    p->emissiveBoost = VFXContrast_ApplyEmissionIntensity(
        p->emissiveBoost, p->contrastProfile);
  p->radiusCurve = config.radiusCurve;
  p->speedCurve = config.speedCurve;
  p->alphaCurve = config.alphaCurve;
  p->emissiveCurve = config.emissiveCurve;
  p->rotation = config.rotation;
  p->angularVelocity = config.angularVelocity;
  p->active = true;

  if (config.onDeathEmit && config.onDeathEmitCount > 0)
  {
    p->onDeathConfig = *config.onDeathEmit;
    p->onDeathConfig.onDeathEmit = NULL;
    p->onDeathConfig.onLiveEmit = NULL;
    p->onDeathCount = config.onDeathEmitCount;
    p->hasDeathEmit = true;
  }
  else
  {
    p->hasDeathEmit = false;
  }

  if (config.onLiveEmit && config.onLiveEmitRate > 0.0f)
  {
    p->onLiveConfig = *config.onLiveEmit;
    p->onLiveConfig.onLiveEmit = NULL;
    p->onLiveConfig.onDeathEmit = NULL;
    p->onLiveEmitRate = config.onLiveEmitRate;
    p->onLiveEmitTimer = 0.0f;
    p->hasLiveEmit = true;
  }
  else
  {
    p->hasLiveEmit = false;
  }

  // Populate stretch, collision, and trail parameters
  p->stretchStrength = config.render.stretchStrength;
  p->stretchMinSpeed = config.render.stretchMinSpeed;

  p->collisionEnabled = config.physics.collisionEnabled;
  p->collisionElasticity = config.physics.collisionElasticity;
  p->collisionFloorY = config.physics.collisionFloorY;
  if (config.physics.onCollisionEmit && config.physics.onCollisionEmitCount > 0)
  {
    p->onCollisionConfig = *config.physics.onCollisionEmit;
    p->onCollisionConfig.onDeathEmit = NULL;
    p->onCollisionConfig.onLiveEmit = NULL;
    p->onCollisionConfig.physics.onCollisionEmit = NULL;
    p->onCollisionCount = config.physics.onCollisionEmitCount;
    p->hasCollisionEmit = true;
  }
  else
  {
    p->hasCollisionEmit = false;
  }

  p->trailLength = config.render.trailLength;
  if (p->trailLength > 8) p->trailLength = 8; // clamp to static buffer size
  p->trailWidthRatio = config.render.trailWidthRatio;
  p->trailColorStart = config.render.trailColorStart;
  p->trailColorEnd = config.render.trailColorEnd;
  p->trailStepTime = (config.render.trailStepTime > 0.0f) ? config.render.trailStepTime : 0.015f;
  p->trailOnly = (p->trailLength > 0) ? config.render.trailOnly : 0;
  p->trailSmooth = config.render.trailSmooth;
  p->trailHistoryCount = 0;
  p->trailHistoryTimer = 0.0f;
}

void UpdateParticles(float dt)
{
  s_particleTime += dt;
  const bool windActive = WindZone_IsActive();

  // TỐI ƯU 2: Lặp ngược (Reverse Loop).
  // Việc này giúp xóa hạt an toàn (Swap-Remove không bị sót phần tử)
  // và BỎ QUA các hạt mới sinh trong chính frame này.
  for (int a = s_activeCount - 1; a >= 0; a--)
  {
    int i = s_activeIds[a];
    ParticleInternal *p = &g_Particles[i];

    p->lifetime -= dt;
    p->rotation += p->angularVelocity * dt;

    if (p->lifetime <= 0.0f)
    {
      if (p->hasDeathEmit)
      {
        for (int c = 0; c < p->onDeathCount; c++)
        {
          ParticleConfig tempChild = p->onDeathConfig;
          tempChild.position = (Vector3){p->x, p->y, p->z};
          // Thừa hưởng vận tốc từ hạt mẹ (velocity inheritance)
          tempChild.velocity.x += p->vx * p->onDeathConfig.velocityInheritance;
          tempChild.velocity.y += p->vy * p->onDeathConfig.velocityInheritance;
          tempChild.velocity.z += p->vz * p->onDeathConfig.velocityInheritance;

          // Sử dụng float random nhanh qua Random01() thay cho GetRandomValue chậm
          tempChild.velocity.x += (Random01() * 160.0f - 80.0f);
          tempChild.velocity.y += (Random01() * 160.0f - 80.0f);
          tempChild.velocity.z += (Random01() * 160.0f - 80.0f);
          SpawnParticle(tempChild);
        }
      }
      Particle_Deactivate(i);
      continue;
    }

    // Follow carries the emitter's *displacement*, not its absolute position:
    // the puff stays a puff after release instead of snapping back to the
    // source every frame. A generation mismatch means a static pool slot has
    // been recycled; detach safely rather than gluing old smoke to new fire.
    if (p->followTarget != NULL &&
        (p->followTargetGeneration == NULL ||
         *p->followTargetGeneration == p->followGeneration))
    {
      Vector3 now = *p->followTarget;
      float ageT = 1.0f - Clamp(p->lifetime / p->maxLifetime, 0.0f, 1.0f);
      float strength = p->followStrength *
          (p->followCurve ? SkillCurve_Eval(p->followCurve, ageT) : (1.0f - ageT));
      p->x += (now.x - p->followTargetLast.x) * strength;
      p->y += (now.y - p->followTargetLast.y) * strength;
      p->z += (now.z - p->followTargetLast.z) * strength;
      p->followTargetLast = now;
    }
    else if (p->followTargetGeneration != NULL)
    {
      p->followTarget = NULL;
    }

    if (p->hasLiveEmit)
    {
      p->onLiveEmitTimer += dt;
      float spawnInterval = 1.0f / p->onLiveEmitRate;
      int safetyCounter = 0;
      float totalT = p->onLiveEmitTimer;

      float speedMul = 1.0f;
      if (p->speedCurve)
      {
        float ageT = 1.0f - Clamp(p->lifetime / p->maxLifetime, 0.0f, 1.0f);
        speedMul = SkillCurve_Eval(p->speedCurve, ageT);
      }
      float stepX = p->vx * dt * speedMul;
      float stepY = p->vy * dt * speedMul;
      float stepZ = p->vz * dt * speedMul;

      while (p->onLiveEmitTimer >= spawnInterval && safetyCounter < 10)
      {
        p->onLiveEmitTimer -= spawnInterval;
        float t = (totalT - p->onLiveEmitTimer) / totalT;

        ParticleConfig tempLive = p->onLiveConfig;
        tempLive.position = (Vector3){
            p->x - stepX * (1.0f - t),
            p->y - stepY * (1.0f - t),
            p->z - stepZ * (1.0f - t)
        };

        // Thừa hưởng vận tốc từ hạt mẹ (velocity inheritance)
        tempLive.velocity.x += p->vx * tempLive.velocityInheritance;
        tempLive.velocity.y += p->vy * tempLive.velocityInheritance;
        tempLive.velocity.z += p->vz * tempLive.velocityInheritance;

        SpawnParticle(tempLive);
        safetyCounter++;
      }
      if (p->onLiveEmitTimer >= spawnInterval)
        p->onLiveEmitTimer = 0.0f; // Reset backlog an toàn
    }

    // TỐI ƯU 3: Inline Math Scalar (Tính toán trục tiếp trên x, y, z)
    if (p->forceField)
    {
      Vector3 pos = {p->x, p->y, p->z};
      Vector3 vel = {p->vx, p->vy, p->vz};
      Vector3 force = ForceField_Evaluate(p->forceField, pos, vel, p->lifetime, (Vector3){0}, (Vector3){0});
      p->vx += force.x * dt;
      p->vy += force.y * dt;
      p->vz += force.z * dt;

      float viscDamp = ForceField_GetViscosityDamping(p->forceField, dt);
      p->vx *= viscDamp;
      p->vy *= viscDamp;
      p->vz *= viscDamp;
    }

    if (windActive)
    {
      Vector3 windForce = WindZone_Evaluate((Vector3){p->x, p->y, p->z}, (Vector3){p->vx, p->vy, p->vz}, s_particleTime);
      p->vx += windForce.x * dt;
      p->vy += windForce.y * dt;
      p->vz += windForce.z * dt;
    }

    float speedMul = 1.0f;
    if (p->speedCurve)
    {
      float ageT = 1.0f - Clamp(p->lifetime / p->maxLifetime, 0.0f, 1.0f);
      speedMul = SkillCurve_Eval(p->speedCurve, ageT);
    }

    float step = dt * speedMul;
    p->x += p->vx * step;
    p->y += p->vy * step;
    p->z += p->vz * step;

    // Ground Collision
    if (p->collisionEnabled && p->y <= p->collisionFloorY)
    {
      p->vy = -p->vy * p->collisionElasticity;
      p->vx *= 0.75f;
      p->vz *= 0.75f;
      p->y = p->collisionFloorY + 0.005f;

      if (p->hasCollisionEmit && p->onCollisionCount > 0)
      {
        for (int c = 0; c < p->onCollisionCount; c++)
        {
          ParticleConfig tempColl = p->onCollisionConfig;
          tempColl.position = (Vector3){p->x, p->collisionFloorY + 0.01f, p->z};
          float ang = (Random01() * 360.0f) * DEG2RAD;
          float spd = (Random01() * 2.0f + 1.0f);
          tempColl.velocity.x += cosf(ang) * spd;
          tempColl.velocity.y += (Random01() * 2.5f + 1.0f);
          tempColl.velocity.z += sinf(ang) * spd;
          SpawnParticle(tempColl);
        }
      }
    }

    // Particle Trail History Update
    if (p->trailLength > 0)
    {
      p->trailHistoryTimer += dt;
      if (p->trailHistoryTimer >= p->trailStepTime || p->trailHistoryCount == 0)
      {
        p->trailHistoryTimer = 0.0f;
        int maxLen = p->trailLength;
        if (maxLen > 8) maxLen = 8;
        int limit = (p->trailHistoryCount < maxLen) ? p->trailHistoryCount : maxLen - 1;
        for (int h = limit; h > 0; h--)
        {
          p->trailHistory[h] = p->trailHistory[h - 1];
        }
        p->trailHistory[0] = (Vector3){p->x, p->y, p->z};
        if (p->trailHistoryCount < maxLen)
        {
          p->trailHistoryCount++;
        }
      }
    }
  }
}

static float s_particleDepths[MAX_PARTICLES];

static void SortParticlesByDepth(int *ids, int count, const float *depths)
{
  for (int gap = count / 2; gap > 0; gap /= 2)
  {
    for (int i = gap; i < count; i++)
    {
      int temp = ids[i];
      float tempDepth = depths[temp];
      int j;
      for (j = i; j >= gap && depths[ids[j - gap]] > tempDepth; j -= gap)
      {
        ids[j] = ids[j - gap];
      }
      ids[j] = temp;
    }
  }
}

// ─── Đợt E / F1 — lit particles ──────────────────────────────────────────────
// Flat-shaded smoke can only ever look like a decal OF smoke; volume reads from
// lighting. See core/docs/ELDEN_VFX_SPEC.md §0.1b.
//
// Scope note: this lights the whole CPU batch with ONE strength, not per
// particle. The batch is a single immediate-mode rlBegin(RL_QUADS) run with no
// spare vertex channel, so a per-particle value cannot be smuggled through.
// That is not the compromise it looks like — it lines up with F1b's blend law:
// alpha-blended bodies (smoke, dust, ash) want lighting, additive emitters
// (embers, sparks, glow) emit their own and must stay unlit. Callers that draw
// additive simply leave the strength at 0.
//
// Default is 0.0 = the exact pre-F1 look, so nothing already shipped moves.
// (s_lightingStrength / s_scatterStrength are declared above InitParticleSystem,
// which registers them as hot-reloadable tunables.)
static Shader s_litShader = {0};
static bool   s_litShaderTried = false;
static bool   s_litActive = false;
static int s_locSunToLight, s_locSunColor, s_locAmbient, s_locViewPos;
static int s_locLightStrength, s_locScatterStrength;
static int s_locVfxCount, s_locVfxPos, s_locVfxColor, s_locVfxRadius;
static int s_locDebugNormal, s_locNormalBulge, s_locSunGain, s_locAmbientGain;
static int s_locLightAzimuth, s_locAnalyticUV;
static int s_locVolumeSheet = -1, s_locRampLUT = -1, s_locHeatGain = -1,
           s_locSmokeTint = -1, s_locSmokeGain = -1;

#define PARTICLE_MAX_VFX_LIGHTS 4

void ParticleSystem_SetLighting(float strength01, float scatter01)
{
  s_lightingStrength = strength01 < 0.0f ? 0.0f : (strength01 > 1.0f ? 1.0f : strength01);
  s_scatterStrength  = scatter01  < 0.0f ? 0.0f : (scatter01  > 2.0f ? 2.0f : scatter01);
}

void ParticleSystem_GetLighting(float *outStrength, float *outScatter)
{
  if (outStrength) *outStrength = s_lightingStrength;
  if (outScatter)  *outScatter  = s_scatterStrength;
}

static Vector3 ColorToVec3_Particle(Color c)
{
  return (Vector3){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
}

// Registered on first draw, NOT from InitParticleSystem. main.c calls
// InitParticleSystem (:1017) well before Tuning_Init (:1063), and
// Tuning_RegisterFloat only reads the config file when the path is already
// set — registering early therefore silently keeps the default and the feature
// looks dead until someone happens to re-save tuning.cfg. By first draw the
// path is set. See core/docs/LANDMINES.md.
static bool s_tunablesRegistered = false;

static void ParticleLighting_Begin(Camera3D camera)
{
  if (!s_tunablesRegistered)
  {
    s_tunablesRegistered = true;
    Tuning_RegisterFloat("particle_lighting_strength", &s_lightingStrength, s_lightingStrength);
    Tuning_RegisterFloat("particle_scatter_strength", &s_scatterStrength, s_scatterStrength);
    Tuning_RegisterFloat("particle_debug_normal", &s_debugNormal, s_debugNormal);
    Tuning_RegisterFloat("particle_normal_bulge", &s_normalBulge, s_normalBulge);
    Tuning_RegisterFloat("particle_sun_gain", &s_sunGain, s_sunGain);
    Tuning_RegisterFloat("particle_ambient_gain", &s_ambientGain, s_ambientGain);
    Tuning_RegisterFloat("particle_light_azimuth", &s_lightAzimuth, s_lightAzimuth);
    Tuning_RegisterFloat("particle_analytic_uv", &s_analyticUV, s_analyticUV);
    Tuning_RegisterFloat("particle_emissive_boost", &s_emissiveBoost, 1.0f);
    Tuning_RegisterFloat("particle_soft_fade", &s_softFade, 0.35f);
    Tuning_RegisterFloat("particle_soft_debug", &s_softDebug, 0.0f);
  }

  s_litActive = false;
  // Clamp HERE, not only in the setter: a tuning.cfg hot-reload writes the
  // float directly and never goes through ParticleSystem_SetLighting.
  if (s_lightingStrength < 0.0f) s_lightingStrength = 0.0f;
  else if (s_lightingStrength > 1.0f) s_lightingStrength = 1.0f;
  if (s_scatterStrength < 0.0f) s_scatterStrength = 0.0f;
  else if (s_scatterStrength > 2.0f) s_scatterStrength = 2.0f;

  // State report — re-emitted whenever a value CHANGES, not just once. The
  // first version logged one line at startup, which was useless for the actual
  // workflow: tuning.cfg hot-reloads, so the interesting values are the ones
  // that arrive AFTER that line has already scrolled past. With no confirmation
  // of what is live, an edit that never took effect is indistinguishable from an
  // edit that took effect and did nothing.
  {
    static float last[5] = {-999, -999, -999, -999, -999};
    const float now[5] = {s_lightingStrength, s_scatterStrength, s_normalBulge,
                          s_lightAzimuth, s_analyticUV};
    bool changed = false;
    for (int i = 0; i < 5; i++)
      if (fabsf(now[i] - last[i]) > 1e-4f) changed = true;
    if (changed)
    {
      for (int i = 0; i < 5; i++) last[i] = now[i];
      TraceLog(LOG_INFO,
               "PARTICLE F1: strength=%.2f scatter=%.2f bulge=%.2f azimuth=%.1f "
               "analyticUV=%.0f sunGain=%.2f ambGain=%.2f quality=%d%s",
               s_lightingStrength, s_scatterStrength, s_normalBulge, s_lightAzimuth,
               s_analyticUV, s_sunGain, s_ambientGain, (int)GfxQuality_Get(),
               (s_lightingStrength <= 0.0f)  ? "  -> OFF (strength is 0)" :
               (GfxQuality_Get() <= GFX_LOW) ? "  -> OFF (needs GfxQuality >= MED)" :
                                               "  -> lit path ACTIVE");
    }
  }

  // The shader's unlit branch is also the soft-particle path. Do not make the
  // ground-intersection fade depend on optional lighting being enabled.
  bool wantSoftParticles = (s_softFade > 0.0f && ScreenDistort_GetDepthTexture().id != 0);
  // A packed volume sheet is NOT an enhancement that may be gated away — it is
  // the only thing that can decode the texture. Its RGB is three density
  // channels, so the default shader would paint it as a colour and the fire
  // would come out green. Whenever one is on screen this shader must bind,
  // whatever the lighting strength or the quality tier says.
  bool wantVolume = ParticleSystem_HasVolumeParticles();
  if (s_lightingStrength <= 0.0f && !wantSoftParticles && !wantVolume)
    return;
  // Per-fragment lighting on every particle is real fill-rate; the Mali devices
  // are the constraint (ENGINE_LANDMINES.md). LOW/UNLIT keep the cheap path.
  if (GfxQuality_Get() <= GFX_LOW && !wantSoftParticles && !wantVolume)
    return;

  if (!s_litShaderTried)
  {
    s_litShaderTried = true;
    s_litShader = ResourceManager_LoadShader("core/particles/shaders/particle_lit.vs",
                                             "core/particles/shaders/particle_lit.fs");
    if (s_litShader.id != 0)
    {
      s_locSunToLight      = GetShaderLocation(s_litShader, "u_sunToLight");
      s_locSunColor        = GetShaderLocation(s_litShader, "u_sunColor");
      s_locAmbient         = GetShaderLocation(s_litShader, "u_ambient");
      s_locViewPos         = GetShaderLocation(s_litShader, "viewPos");
      s_locLightStrength   = GetShaderLocation(s_litShader, "u_lightingStrength");
      s_locScatterStrength = GetShaderLocation(s_litShader, "u_scatterStrength");
      s_locVfxCount        = GetShaderLocation(s_litShader, "u_vfxLightCount");
      s_locVfxPos          = GetShaderLocation(s_litShader, "u_vfxLightPos");
      s_locVfxColor        = GetShaderLocation(s_litShader, "u_vfxLightColor");
      s_locVfxRadius       = GetShaderLocation(s_litShader, "u_vfxLightRadius");
      s_locDebugNormal     = GetShaderLocation(s_litShader, "u_debugNormal");
      s_locNormalBulge     = GetShaderLocation(s_litShader, "u_normalBulge");
      s_locSunGain         = GetShaderLocation(s_litShader, "u_sunGain");
      s_locAmbientGain     = GetShaderLocation(s_litShader, "u_ambientGain");
      s_locLightAzimuth    = GetShaderLocation(s_litShader, "u_lightAzimuth");
      s_locAnalyticUV      = GetShaderLocation(s_litShader, "u_analyticUV");
      s_locSoftFade        = GetShaderLocation(s_litShader, "u_softFade");
      s_locSoftDebug       = GetShaderLocation(s_litShader, "u_softDebug");
      s_locAtlasGrid       = GetShaderLocation(s_litShader, "u_atlasGrid");
      s_locEmissiveBoost   = GetShaderLocation(s_litShader, "u_emissiveBoost");
      s_locVolumeSheet     = GetShaderLocation(s_litShader, "u_volumeSheet");
      s_locRampLUT         = GetShaderLocation(s_litShader, "u_rampLUT");
      s_locHeatGain        = GetShaderLocation(s_litShader, "u_heatGain");
      s_locSmokeTint       = GetShaderLocation(s_litShader, "u_smokeTint");
      s_locSmokeGain       = GetShaderLocation(s_litShader, "u_smokeGain");
    }
    else
    {
      // Never fail silently — a skipped shader that logs nothing has cost this
      // project a full debugging session before (rlvk_shaderc.inl:1129).
      TraceLog(LOG_WARNING,
               "PARTICLE: particle_lit shader failed to load — particles stay unlit");
    }
  }
  // DID IT ACTUALLY COMPILE? A non-zero id is not the same answer. raylib hands
  // back the DEFAULT shader when compilation fails, and rlvk logs the GLSL error
  // and carries on, so `id != 0` was true for a shader that did not exist —
  // measured the hard way: one misplaced uniform declaration killed the lit
  // path, the emissive boost and the soft fade at once, while this file happily
  // reported "soft-fade: ON" for several rounds of debugging.
  //
  // The test is uniforms this shader definitely declares. If none of them
  // resolve, whatever is bound is not ours.
  if (s_litShader.id != 0 && s_locSunToLight < 0 && s_locLightStrength < 0 &&
      s_locAmbient < 0)
  {
    TraceLog(LOG_ERROR, "PARTICLE: particle_lit.fs loaded (id %u) but NONE of its "
                        "uniforms resolved — it did not compile. Falling back to "
                        "unlit; check the GLSL error above this line.",
             (unsigned)s_litShader.id);
    s_litShader.id = 0;
  }
  if (s_litShader.id == 0)
    return;

  Vector3 sunToLight = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
  Vector3 sunColor   = ColorToVec3_Particle(Environment_GetSunColor());
  Vector3 ambient    = ColorToVec3_Particle(Environment_GetAmbientColor());

  BeginShaderMode(s_litShader);
  s_litActive = true;

  SetShaderValue(s_litShader, s_locSunToLight, &sunToLight, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locSunColor, &sunColor, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locAmbient, &ambient, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locViewPos, &camera.position, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locLightStrength, &s_lightingStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locScatterStrength, &s_scatterStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locDebugNormal, &s_debugNormal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locNormalBulge, &s_normalBulge, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locSunGain, &s_sunGain, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locAmbientGain, &s_ambientGain, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locLightAzimuth, &s_lightAzimuth, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locAnalyticUV, &s_analyticUV, SHADER_UNIFORM_FLOAT);
  { float one = 1.0f;   // lit default; flipped per batch for emissive particles
    if (s_locEmissiveBoost >= 0) SetShaderValue(s_litShader, s_locEmissiveBoost, &one, SHADER_UNIFORM_FLOAT); }
  // SOFT PARTICLES — bind the previous frame's linearised scene depth.
  //
  // OFF unless there is really a depth texture to sample. The failure mode of
  // getting this wrong is total: an unbound sampler reads 0, the factor is then
  // 0 everywhere, and EVERY particle in the game disappears. So the C side
  // decides, and the shader treats 0 as "feature off" rather than "fully
  // occluded".
  {
    Texture2D depthTex = ScreenDistort_GetDepthTexture();
    float fade = (depthTex.id != 0 && s_softFade > 0.0f && s_locSoftFade >= 0)
                     ? s_softFade : 0.0f;
    if (fade > 0.0f && depthTex.id != 0)
      ScreenDistort_BindDepthForSoftParticles(s_litShader, PARTICLE_SOFT_DEPTH_SLOT);
    if (s_locSoftFade >= 0)
      SetShaderValue(s_litShader, s_locSoftFade, &fade, SHADER_UNIFORM_FLOAT);
    if (s_locSoftDebug >= 0)
      SetShaderValue(s_litShader, s_locSoftDebug, &s_softDebug, SHADER_UNIFORM_FLOAT);
    // The debug view needs the depth bound even when the fade itself is off,
    // or it would paint the answer to a question nobody asked.
    if (fade <= 0.0f && s_softDebug > 0.5f && depthTex.id != 0)
    {
      ScreenDistort_BindDepthForSoftParticles(s_litShader, PARTICLE_SOFT_DEPTH_SLOT);
      s_softBound = true;
    }
    s_softBound = (fade > 0.0f || (s_softDebug > 0.5f && depthTex.id != 0));
    // Announce on CHANGE: "the cut is still there" has to be separable from
    // "the fade is running and is too small".
    static int lastState = -1;
    int state = (int)(fade > 0.0f) + ((depthTex.id != 0) ? 2 : 0);
    if (state != lastState)
    {
      lastState = state;
      TraceLog(depthTex.id != 0 ? LOG_INFO : LOG_WARNING,
               "PARTICLE soft-fade: %s (depth tex %u, fade %.2f m)",
               (fade > 0.0f) ? "ON" : "OFF", (unsigned)depthTex.id, fade);
    }
  }
  { // default: not an atlas. Flipped per batch in the draw loop.
    float grid[2] = {1.0f, 1.0f};
    if (s_locAtlasGrid >= 0) SetShaderValue(s_litShader, s_locAtlasGrid, grid, SHADER_UNIFORM_VEC2);
  }
  { // default: legacy colour sheet. Flipped per batch in the draw loop.
    float off = 0.0f, one = 1.0f;
    if (s_locVolumeSheet >= 0) SetShaderValue(s_litShader, s_locVolumeSheet, &off, SHADER_UNIFORM_FLOAT);
    if (s_locHeatGain >= 0)    SetShaderValue(s_litShader, s_locHeatGain, &one, SHADER_UNIFORM_FLOAT);
    if (s_locSmokeGain >= 0) SetShaderValue(s_litShader, s_locSmokeGain, &one, SHADER_UNIFORM_FLOAT);
    if (s_locSmokeTint >= 0)
    {
      // Soot, not neutral grey. The volume path multiplies this by the scene
      // light, so a white tint would make the smoke half read as steam. Flipped
      // per batch below for effects that author their own smoke colour.
      float tint[3] = {0.32f, 0.29f, 0.27f};
      SetShaderValue(s_litShader, s_locSmokeTint, tint, SHADER_UNIFORM_VEC3);
    }
  }

  // VFX point lights — the caster's own fireball lighting the smoke inside it.
  VFXLightData lights[PARTICLE_MAX_VFX_LIGHTS];
  int count = 0;
  VFXLight_GetActive(lights, &count, PARTICLE_MAX_VFX_LIGHTS);
  if (count > PARTICLE_MAX_VFX_LIGHTS) count = PARTICLE_MAX_VFX_LIGHTS;

  float pos[PARTICLE_MAX_VFX_LIGHTS * 3] = {0};
  float col[PARTICLE_MAX_VFX_LIGHTS * 3] = {0};
  float rad[PARTICLE_MAX_VFX_LIGHTS] = {0};
  for (int i = 0; i < count; i++)
  {
    pos[i * 3 + 0] = lights[i].position.x;
    pos[i * 3 + 1] = lights[i].position.y;
    pos[i * 3 + 2] = lights[i].position.z;
    Vector3 c = ColorToVec3_Particle(lights[i].color);
    col[i * 3 + 0] = c.x;
    col[i * 3 + 1] = c.y;
    col[i * 3 + 2] = c.z;
    rad[i] = lights[i].radius;
  }
  SetShaderValue(s_litShader, s_locVfxCount, &count, SHADER_UNIFORM_INT);
  SetShaderValueV(s_litShader, s_locVfxPos, pos, SHADER_UNIFORM_VEC3, PARTICLE_MAX_VFX_LIGHTS);
  SetShaderValueV(s_litShader, s_locVfxColor, col, SHADER_UNIFORM_VEC3, PARTICLE_MAX_VFX_LIGHTS);
  SetShaderValueV(s_litShader, s_locVfxRadius, rad, SHADER_UNIFORM_FLOAT, PARTICLE_MAX_VFX_LIGHTS);
}

// Push a one-off strength without disturbing the tunable. Used to make emissive
// particles skip lighting mid-batch.
static void ParticleLighting_SetStrength(float v)
{
  if (!s_litActive) return;
  rlDrawRenderBatchActive();
  SetShaderValue(s_litShader, s_locLightStrength, &v, SHADER_UNIFORM_FLOAT);
}

static void ParticleLighting_SetEmissive(float v)
{
  if (!s_litActive || s_locEmissiveBoost < 0) return;
  rlDrawRenderBatchActive();
  SetShaderValue(s_litShader, s_locEmissiveBoost, &v, SHADER_UNIFORM_FLOAT);
}

static void ParticleLighting_End(void)
{
  if (!s_litActive)
    return;
  // Release the texture unit before the shader goes away, or the next system to
  // use slot 3 inherits a bound depth texture.
  if (s_softBound)
  {
    ScreenDistort_UnbindSoftParticleDepth(PARTICLE_SOFT_DEPTH_SLOT);
    s_softBound = false;
  }
  EndShaderMode();
  s_litActive = false;
}

// ── PERF INSTRUMENT ─────────────────────────────────────────────────────────
// Added after two WRONG guesses at where the cost was (first "overdraw", then
// "emission rate"): cutting one flame's live sprites from ~700 to ~18 barely
// moved the frame rate, which means the binding constraint was never the
// particle count. Guessing again is the expensive move; this reports the three
// quantities that tell the cases apart:
//
//   live    — particles alive. If this is small and it is still slow, the cost
//             is NOT particle count.
//   quads   — vertices/4, i.e. how much fill was requested. The cross-fade
//             doubles this against `live`.
//   batches — rlEnd/rlBegin splits, one per change of texture, blend mode,
//             lit flag, atlas grid or emissive boost. Particles are drawn in
//             pool order, so populations INTERLEAVE, and every alternation is a
//             flush. This is the number that would explain "10 bursts kill it
//             but 700 sprites of one flame did not".
// Colours are compared, not blended, in the batch key — pack to one word so a
// changed smoke tint reopens the batch the same way a changed ramp does.
static inline unsigned int VFXPackColor(Color c)
{
  return ((unsigned int)c.r << 24) | ((unsigned int)c.g << 16) |
         ((unsigned int)c.b << 8) | (unsigned int)c.a;
}

static int   s_perfBatches = 0;
static int   s_perfQuads   = 0;
static float s_perfLog     = 0.0f;

static void DrawParticlesLayer(Camera3D camera, Texture2D texture, int layerFilter)
{
  if (s_activeCount == 0)
    return;

  Vector3 viewDir = {camera.position.x - camera.target.x,
                     camera.position.y - camera.target.y,
                     camera.position.z - camera.target.z};
  float viewLen = sqrtf(viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z);
  if (viewLen > 0.0f)
  {
    viewDir.x /= viewLen;
    viewDir.y /= viewLen;
    viewDir.z /= viewLen;
  }

  Vector3 right = {camera.up.y * viewDir.z - camera.up.z * viewDir.y,
                   camera.up.z * viewDir.x - camera.up.x * viewDir.z,
                   camera.up.x * viewDir.y - camera.up.y * viewDir.x};

  float rightLen = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rightLen > 0.0f)
  {
    right.x /= rightLen;
    right.y /= rightLen;
    right.z /= rightLen;
  }

  Vector3 up = {viewDir.y * right.z - viewDir.z * right.y,
                viewDir.z * right.x - viewDir.x * right.z,
                viewDir.x * right.y - viewDir.y * right.x};

  /* Request only the region soft particles can occupy next frame.  The depth
   * copy is therefore proportional to visible VFX coverage, not display size. */
  if (s_softFade > 0.0f)
  {
    Rectangle bounds = {0};
    bool hasBounds = false;
    float screenH = (float)GetScreenHeight();
    float halfFovy = camera.fovy * DEG2RAD * 0.5f;
    for (int a = 0; a < s_activeCount; ++a)
    {
      ParticleInternal *p = &g_Particles[s_activeIds[a]];
      if (p->renderMode == 3 || p->trailOnly) continue;
      Vector3 pos = {p->x, p->y, p->z};
      float distance = Vector3Distance(camera.position, pos);
      if (distance <= 0.01f) continue;
      Vector2 center = GetWorldToScreen(pos, camera);
      // THE DRAWN radius, not the authored one. The draw loop scales by
      // radiusCurve, so a growing sprite is larger than `p->radius` — up to
      // 1.4x for the energy burst. Requesting the smaller region left the outer
      // rim outside the copied depth, where prevDepthTex holds stale data, the
      // soft factor collapses toward 0 and the sprite VANISHES there. The
      // symptom is sharp horizontal and vertical bands cut out of an effect,
      // bounded by the region rectangle, and it hid for as long as it did
      // because a dim alpha body loses those bands invisibly — it only became
      // obvious once an effect was bright and additive.
      float drawRadius = p->radius;
      if (p->radiusCurve)
      {
        float lr = p->lifetime / p->maxLifetime;
        if (lr < 0.0f) lr = 0.0f; else if (lr > 1.0f) lr = 1.0f;
        drawRadius *= SkillCurve_Eval(p->radiusCurve, 1.0f - lr);
      }
      float radiusPx = drawRadius * screenH / (2.0f * distance * tanf(halfFovy));
      Rectangle r = {center.x - radiusPx, center.y - radiusPx, radiusPx * 2.0f, radiusPx * 2.0f};
      if (!hasBounds) { bounds = r; hasBounds = true; }
      else {
        float x1 = fmaxf(bounds.x + bounds.width, r.x + r.width);
        float y1 = fmaxf(bounds.y + bounds.height, r.y + r.height);
        bounds.x = fminf(bounds.x, r.x); bounds.y = fminf(bounds.y, r.y);
        bounds.width = x1 - bounds.x; bounds.height = y1 - bounds.y;
      }
    }
    if (hasBounds) ScreenDistort_RequestSoftDepthRegion(bounds);
  }

  // Sắp xếp các hạt từ xa đến gần (Back-to-Front Depth Sorting)
  for (int a = 0; a < s_activeCount; a++)
  {
    int idx = s_activeIds[a];
    ParticleInternal *p = &g_Particles[idx];
    s_particleDepths[idx] = p->x * viewDir.x + p->y * viewDir.y + p->z * viewDir.z;
  }
  SortParticlesByDepth(s_activeIds, s_activeCount, s_particleDepths);

  // Đồng bộ lại s_slotListIndex do thứ tự trong s_activeIds đã thay đổi sau khi sắp xếp
  for (int a = 0; a < s_activeCount; a++)
  {
    s_slotListIndex[s_activeIds[a]] = a;
  }

  ParticleLighting_Begin(camera);

  // Per-particle textures, batched by walking the ALREADY depth-sorted list and
  // reopening the batch whenever the texture changes. Splitting this way (rather
  // than grouping by texture first) keeps the global back-to-front order intact,
  // which alpha blending requires — grouping would composite a near puff before
  // a far one and show through. Cost is one extra draw call per texture change;
  // with a handful of distinct particle textures that is a few per frame.
  unsigned int curTex = 0xFFFFFFFFu;
  int curBlend = -1;
  int curUnlit = -1;
  // Atlas grid of the batch in flight. Batches already split on texture, and an
  // atlas particle necessarily carries a different texture from a plain one, so
  // this never splits a batch that would not have split anyway.
  int curGridC = -1, curGridR = -1;
  float curBoost = -1.0f;
  int curVolume = -1;
  unsigned int curRamp = 0xFFFFFFFFu;
  float curHeat = -1.0f;
  float curSmokeGain = -1.0f;
  unsigned int curSmokeTint = 0xFFFFFFFFu;

  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];
    // A glow sheet often stores black RGB outside its luminous core. It is
    // valid under additive blending, but becomes a dark halo when forced into
    // an alpha body. Only particles authored as alpha material enter body;
    // additive particles remain in their emission pass.
    // PREMULTIPLIED goes to BODY, not emission, even though it emits.
    //
    // The emission layer is composited with BLEND_ADD_COLORS
    // (screen_distort.c) — factors (ONE, ONE) on colour AND alpha, so the
    // layer's alpha is never read as coverage and nothing in it can darken the
    // scene. A premultiplied particle put there loses exactly the half that
    // justifies the mode: it would only ever add light, which over a bright sky
    // is the milky wash the whole change exists to remove. Verified by raising
    // the coverage substantially and watching the composited frame not change.
    //
    // The BODY composite already does the right thing. distortion.fs computes
    // `scene*(1-a) + (rgb/a)*a`, which is algebraically `scene*(1-a) + rgb` —
    // premultiplied-over exactly. And the body target is the same R16F format
    // as emission, so the HDR headroom the blown-out core needs survives.
    // Nothing about the shared composite has to change, and no other VFX is
    // touched.
    // PREMULTIPLIED goes to EMISSION with additive, not to BODY.
    //
    // It was routed to BODY when the split VFX layers still existed, because
    // the emission TARGET was composited with BLEND_ADD_COLORS and discarded
    // coverage. Those layers were retired (they were arithmetic that cancels);
    // emission now draws straight into the scene, so coverage survives there
    // and the original reason is gone. Drawing it in BODY instead cost sharp
    // horizontal bands — that pass also carries trails, decals and afterimages
    // and their depth-mask handling.
    if (layerFilter == 0 && p->blendMode != VFX_BLEND_ALPHA) continue;
    if (layerFilter == 1 && p->blendMode == VFX_BLEND_ALPHA) continue;
    // SURFACE_INPUT is rendered exclusively by FluidSurface; drawing it here
    // would reveal its source particles as billboards as well.
    if (p->renderMode == 3) continue;
    // Headless wisp: the particle exists to carry a path, not to be a sprite.
    // Skipped before the batching decision so it cannot split a batch either.
    if (p->trailOnly) continue;

    unsigned int want = p->texId ? p->texId : texture.id;
    int wantGridC = (p->spriteAnim ? p->spriteAnim->cols : 1);
    int wantGridR = (p->spriteAnim ? p->spriteAnim->rows : 1);
    // Global multiplier on top of the per-particle value, so the whole look can
    // be dialled without touching every call site.
    float wantBoost = p->emissiveBoost * s_emissiveBoost;
    // The body pass normally forces ALPHA so a glow sheet cannot smear its
    // black surround into the body layer. PREMULTIPLIED is the exception it has
    // to keep: its RGB is already scaled by its own coverage, so forcing it to
    // ALPHA would make the hardware multiply by alpha a second time and the
    // flame would come out roughly its own coverage darker.
    int drawBlend = (layerFilter == 0) ? VFX_BLEND_ALPHA : p->blendMode;
    if (want != curTex || drawBlend != curBlend || p->unlit != curUnlit ||
        wantGridC != curGridC || wantGridR != curGridR || wantBoost != curBoost ||
        p->volumeSheet != curVolume || p->rampTexId != curRamp ||
        p->heatGain != curHeat || p->smokeGain != curSmokeGain ||
        VFXPackColor(p->smokeTint) != curSmokeTint)
    {
      if (curTex != 0xFFFFFFFFu) rlEnd();
      s_perfBatches++;
      if (drawBlend != curBlend)
      {
        // Blend state must be flushed either side or it leaks across the batch
        // boundary — ENGINE_LANDMINES.md §1, the raylib batching hazard.
        rlDrawRenderBatchActive();
        if (curBlend >= 0) EndBlendMode();
        BeginBlendMode(drawBlend == VFX_BLEND_ADDITIVE      ? BLEND_ADDITIVE
                       : drawBlend == VFX_BLEND_PREMULTIPLIED ? BLEND_ALPHA_PREMULTIPLY
                                                              : BLEND_ALPHA);
        rlDrawRenderBatchActive();
        curBlend = drawBlend;
      }
      if (p->volumeSheet != curVolume || p->rampTexId != curRamp ||
          p->heatGain != curHeat || p->smokeGain != curSmokeGain ||
          VFXPackColor(p->smokeTint) != curSmokeTint)
      {
        // The ramp is a SECOND sampler on the same shader. rlvk resolves
        // samplers by reflected name rather than a presumed binding
        // (HANDOFF §7.30), which is what makes texture0 + depth + ramp legal
        // here; the flush is still required, because the uniform and the
        // texture unit must not change under vertices already queued.
        rlDrawRenderBatchActive();
        float vs = p->volumeSheet ? 1.0f : 0.0f;
        float hg = p->heatGain;
        if (s_litActive && s_locVolumeSheet >= 0)
          SetShaderValue(s_litShader, s_locVolumeSheet, &vs, SHADER_UNIFORM_FLOAT);
        if (s_litActive && s_locHeatGain >= 0)
          SetShaderValue(s_litShader, s_locHeatGain, &hg, SHADER_UNIFORM_FLOAT);
        if (s_litActive && p->volumeSheet && p->rampTexId != 0 && s_locRampLUT >= 0)
        {
          Texture2D ramp = {0};
          ramp.id = p->rampTexId;
          ramp.width = 1; ramp.height = 1; ramp.mipmaps = 1;
          ramp.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
          SetShaderValueTexture(s_litShader, s_locRampLUT, ramp);
        }
        if (s_litActive && s_locSmokeGain >= 0)
        {
          float sg = p->smokeGain;
          SetShaderValue(s_litShader, s_locSmokeGain, &sg, SHADER_UNIFORM_FLOAT);
        }
        if (s_litActive && s_locSmokeTint >= 0)
        {
          float st[3] = {(float)p->smokeTint.r / 255.0f,
                         (float)p->smokeTint.g / 255.0f,
                         (float)p->smokeTint.b / 255.0f};
          SetShaderValue(s_litShader, s_locSmokeTint, st, SHADER_UNIFORM_VEC3);
        }
        curVolume = p->volumeSheet;
        curRamp = p->rampTexId;
        curHeat = p->heatGain;
        curSmokeGain = p->smokeGain;
        curSmokeTint = VFXPackColor(p->smokeTint);
      }
      if (p->unlit != curUnlit)
      {
        // Emissive particles skip the lighting multiply. Flipping the STRENGTH
        // uniform rather than unbinding the shader keeps one pipeline: at 0 the
        // shader takes its early-out and returns texel*colour, which is exactly
        // the legacy unlit result.
        ParticleLighting_SetStrength(p->unlit ? 0.0f : s_lightingStrength);
        curUnlit = p->unlit;
      }
      if (wantBoost != curBoost)
      {
        // Occluding particles never emit: boosting smoke would make it give off
        // light it is supposed to block (the F1b blend law).
        //
        // A VOLUME sheet is the exception, and it is not a loophole: it is not
        // lit-or-emissive but BOTH, split per texel. The shader applies the
        // boost to the emission channel only and leaves the soot half on the
        // scene light, so gating on `unlit` here would silence exactly the
        // flame it is meant to scale — while `unlit` must stay 0 or the soot
        // would go unshaded.
        ParticleLighting_SetEmissive((p->unlit || p->volumeSheet) ? wantBoost : 1.0f);
        curBoost = wantBoost;
      }
      if (wantGridC != curGridC || wantGridR != curGridR)
      {
        // Tell the lighting which grid the UVs live on, so it can recover the
        // quad-local coordinate. Without this an atlas sprite is shaded from a
        // lopsided slice of the hemisphere that jumps every frame step.
        float grid[2] = {(float)wantGridC, (float)wantGridR};
        if (s_locAtlasGrid >= 0)
          SetShaderValue(s_litShader, s_locAtlasGrid, grid, SHADER_UNIFORM_VEC2);
        curGridC = wantGridC;
        curGridR = wantGridR;
      }
      rlSetTexture(want);
      rlBegin(RL_QUADS);
      curTex = want;
    }

    float lifeRatio = p->lifetime / p->maxLifetime;
    if (lifeRatio < 0.0f)
      lifeRatio = 0.0f;
    else if (lifeRatio > 1.0f)
      lifeRatio = 1.0f;

    float invRatio = 1.0f - lifeRatio;
    Color c = p->colorStart;

    if (p->gradient)
    {
      c = ColorGradient_Sample(p->gradient, invRatio);
    }
    else
    {
      c.r = (unsigned char)((int)p->colorStart.r * lifeRatio + (int)p->colorEnd.r * invRatio);
      c.g = (unsigned char)((int)p->colorStart.g * lifeRatio + (int)p->colorEnd.g * invRatio);
      c.b = (unsigned char)((int)p->colorStart.b * lifeRatio + (int)p->colorEnd.b * invRatio);
      c.a = (unsigned char)((int)p->colorStart.a * lifeRatio + (int)p->colorEnd.a * invRatio);
    }

    if (p->alphaCurve)
    {
      float mul = SkillCurve_Eval(p->alphaCurve, invRatio);
      int a_val = (int)((float)p->colorStart.a * mul);
      c.a = (unsigned char)(a_val < 0 ? 0 : (a_val > 255 ? 255 : a_val));
    }

    if (p->emissiveCurve)
    {
      float mul = SkillCurve_Eval(p->emissiveCurve, invRatio);
      int r = (int)((float)c.r * mul);
      c.r = (unsigned char)(r > 255 ? 255 : r);
      int g = (int)((float)c.g * mul);
      c.g = (unsigned char)(g > 255 ? 255 : g);
      int b = (int)((float)c.b * mul);
      c.b = (unsigned char)(b > 255 ? 255 : b);
    }

    c = VFXContrast_ApplyColor(
        c, p->contrastProfile,
        p->blendMode != VFX_BLEND_ALPHA
            ? VFX_CONTRAST_EMISSION
            : VFX_CONTRAST_BODY);

    // ── VOLUME SHEET: the vertex colour changes meaning ──────────────────────
    //
    // Hue now comes from the ramp LUT, sampled per texel, so the RGB slot is
    // free — and it is the ONLY per-particle channel left (rlgl's immediate
    // batch carries position/texcoord/colour and nothing else). It carries the
    // particle's HEAT over its life instead: a grey level the shader multiplies
    // into the sheet's emission before the ramp lookup, so a cooling ember
    // slides down the ramp from white through orange to soot while the sheet
    // supplies the spatial variation. Alpha keeps its usual meaning.
    //
    // Source is emissiveCurve, which in legacy mode scales RGB brightness — the
    // same intent, so a composition that already authored one reads correctly.
    if (p->volumeSheet)
    {
      float heat = 1.0f;
      if (p->emissiveCurve)
        heat = SkillCurve_Eval(p->emissiveCurve, invRatio);
      if (heat < 0.0f) heat = 0.0f;
      else if (heat > 1.0f) heat = 1.0f;
      unsigned char h = (unsigned char)(heat * 255.0f);
      c.r = c.g = c.b = h;
    }

    float drawRadius = p->radius;
    if (p->radiusCurve)
    {
      drawRadius *= SkillCurve_Eval(p->radiusCurve, invRatio);
    }

    float rx = right.x * drawRadius, ry = right.y * drawRadius, rz = right.z * drawRadius;
    float ux = up.x * drawRadius, uy = up.y * drawRadius, uz = up.z * drawRadius;

    // Velocity-stretch rendering (directional billboard)
    if (p->stretchStrength > 0.0f)
    {
      Vector3 vel = {p->vx, p->vy, p->vz};
      float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
      if (speed > p->stretchMinSpeed)
      {
        Vector3 velDir = {vel.x / speed, vel.y / speed, vel.z / speed};
        Vector3 tangent = velDir;
        Vector3 rVec = {
          tangent.y * viewDir.z - tangent.z * viewDir.y,
          tangent.z * viewDir.x - tangent.x * viewDir.z,
          tangent.x * viewDir.y - tangent.y * viewDir.x
        };
        float rVecLen = sqrtf(rVec.x * rVec.x + rVec.y * rVec.y + rVec.z * rVec.z);
        if (rVecLen > 0.0f)
        {
          rVec.x /= rVecLen;
          rVec.y /= rVecLen;
          rVec.z /= rVecLen;
        }
        else
        {
          rVec = right;
        }

        float stretchFactor = 1.0f + speed * p->stretchStrength;
        rx = rVec.x * drawRadius;
        ry = rVec.y * drawRadius;
        rz = rVec.z * drawRadius;

        ux = tangent.x * drawRadius * stretchFactor;
        uy = tangent.y * drawRadius * stretchFactor;
        uz = tangent.z * drawRadius * stretchFactor;
      }
    }
    // Xoay hạt quanh trục hướng camera (Billboard-space 2D Rotation, only if not stretched)
    else if (p->rotation != 0.0f)
    {
      float cosT = cosf(p->rotation);
      float sinT = sinf(p->rotation);

      float rxRot = (right.x * cosT + up.x * sinT) * drawRadius;
      float ryRot = (right.y * cosT + up.y * sinT) * drawRadius;
      float rzRot = (right.z * cosT + up.z * sinT) * drawRadius;

      float uxRot = (-right.x * sinT + up.x * cosT) * drawRadius;
      float uyRot = (-right.y * sinT + up.y * cosT) * drawRadius;
      float uzRot = (-right.z * sinT + up.z * cosT) * drawRadius;

      rx = rxRot; ry = ryRot; rz = rzRot;
      ux = uxRot; uy = uyRot; uz = uzRot;
    }

    // Đọc UV từ hoạt cảnh Sprite sheet atlas
    SpriteAnimFrameSample sample = {
        .uv = {0.0f, 0.0f, 1.0f, 1.0f}, .offset = {0}, .scale = {1.0f, 1.0f}};
    SpriteAnimFrameSample sampleNext = sample;
    float     fbBlend = 0.0f;
    if (p->spriteAnim)
    {
      // Phase-shifted age: without the offset every sprite from one burst
      // holds the same frame as every other (see ParticleConfig.spriteAnimPhase).
      float age = (p->maxLifetime - p->lifetime) * p->spriteAnimRate +
                  p->spriteAnimPhase;
      // Đợt E / E4 — cross-faded flipbook. Snapping to whole frames makes an
      // authored sheet read as the sprites flipping back and forth (32 fps atlas
      // against a 60 fps render, ~2 render frames per atlas frame, then a jump to
      // a different simulation state). Blending the two adjacent frames removes
      // the jump entirely.
      sample = SpriteAnim_CalculateFrameSampleBlend(p->spriteAnim, age,
                                                     &sampleNext, &fbBlend);
      // Kill switch + A/B. Cross-fading emits TWO quads per particle, so this is
      // also the lever if the vertex cost ever matters on a weak device.
      // Registered lazily on first use, never from an Init (docs/LANDMINES.md).
      {
        static float s_fbBlendOn = 1.0f;
        static float s_fbBlendMax = 220.0f;
        static bool  s_fbBlendReg = false;
        if (!s_fbBlendReg) { s_fbBlendReg = true;
          Tuning_RegisterFloat("particle_fb_blend", &s_fbBlendOn, 1.0f);
          Tuning_RegisterFloat("particle_fb_blend_max", &s_fbBlendMax, 220.0f); }
        if (s_fbBlendOn <= 0.5f) fbBlend = 0.0f;
        // LOAD SHEDDING. The cross-fade costs a SECOND full-size quad for every
        // animated particle — it exists to hide the ~2-render-frame step of a
        // 25 fps atlas, which is a subtle artifact, while the second quad is a
        // literal doubling of the most expensive thing on screen. Past a live
        // count where fill rate is the binding constraint, the trade inverts:
        // nobody sees the step in a screen full of overlapping sprites, and
        // everybody sees 20 fps. Dropping it is exactly a 2x fill saving on the
        // frames that need it, and it costs nothing on the frames that do not.
        else if ((float)s_activeCount > s_fbBlendMax) fbBlend = 0.0f;
      }
    }

    // The quad, emitted once per frame being cross-faded. rlgl's immediate batch
    // carries position/texcoord/colour only — there is no spare attribute to
    // hand a second UV set plus a blend factor to the shader — so the fade is
    // done the standard way: draw A at (1-t) and B at t.
    //
    // The mid-blend coverage dip this causes (two alpha draws are not exactly a
    // lerp) is ~7% at smoke's 0.28 alpha and is not visible; it would matter for
    // near-opaque sprites, which flipbooks here are not.
    #define PS_EMIT_QUAD(_sample, _a)                                              \
      do {                                                                         \
        if ((_a) > 0) {                                                            \
          const SpriteAnimFrameSample psSample = (_sample);                       \
          const Rectangle psUV = psSample.uv;                                     \
          const float psU0 = p->spriteFlipX ? psUV.x + psUV.width : psUV.x;       \
          const float psU1 = p->spriteFlipX ? psUV.x : psUV.x + psUV.width;       \
          const float psV0 = p->spriteFlipY ? psUV.y + psUV.height : psUV.y;      \
          const float psV1 = p->spriteFlipY ? psUV.y : psUV.y + psUV.height;      \
          const float psRx = rx * psSample.scale.x;                               \
          const float psRy = ry * psSample.scale.x;                               \
          const float psRz = rz * psSample.scale.x;                               \
          const float psUx = ux * psSample.scale.y;                               \
          const float psUy = uy * psSample.scale.y;                               \
          const float psUz = uz * psSample.scale.y;                               \
          const float psX = p->x - 2.0f * (rx * psSample.offset.x +              \
                                           ux * psSample.offset.y);                \
          const float psY = p->y - 2.0f * (ry * psSample.offset.x +              \
                                           uy * psSample.offset.y);                \
          const float psZ = p->z - 2.0f * (rz * psSample.offset.x +              \
                                           uz * psSample.offset.y);                \
          rlColor4ub(c.r, c.g, c.b, (unsigned char)(_a));                           \
          /* V runs DOWN the image while +up runs UP in the world, so the      \
             top-left texel must land on the TOP vertex. It used to be paired    \
             with the bottom one, i.e. every particle sprite was drawn flipped   \
             vertically. Nobody noticed because every sprite this engine had was \
             round and symmetric; the E4 flame flipbook, which has an UP, came   \
             out upside down. */                                                 \
          rlTexCoord2f(psU0, psV1);                                               \
          rlVertex3f(psX + psRx - psUx, psY + psRy - psUy, psZ + psRz - psUz);    \
          rlTexCoord2f(psU0, psV0);                                               \
          rlVertex3f(psX + psRx + psUx, psY + psRy + psUy, psZ + psRz + psUz);    \
          rlTexCoord2f(psU1, psV0);                                               \
          rlVertex3f(psX - psRx + psUx, psY - psRy + psUy, psZ - psRz + psUz);    \
          rlTexCoord2f(psU1, psV1);                                               \
          rlVertex3f(psX - psRx - psUx, psY - psRy - psUy, psZ - psRz - psUz);    \
        }                                                                          \
      } while (0)

    if (fbBlend > 0.001f)
    {
      PS_EMIT_QUAD(sample,     (float)c.a * (1.0f - fbBlend));
      PS_EMIT_QUAD(sampleNext, (float)c.a * fbBlend);
      s_perfQuads += 2;
    }
    else
    {
      PS_EMIT_QUAD(sample, (float)c.a);
      s_perfQuads++;
    }
    #undef PS_EMIT_QUAD
  }

  if (curTex != 0xFFFFFFFFu) rlEnd();
  if (curBlend >= 0)
  {
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlDrawRenderBatchActive();
  }

  ParticleLighting_End();

  // One line a second, and only when asked for. Registered lazily on first use,
  // never from an Init (docs/LANDMINES.md).
  {
    static float s_perfOn = 0.0f;
    static bool  s_perfReg = false;
    if (!s_perfReg) { s_perfReg = true;
      Tuning_RegisterFloat("particle_perf_log", &s_perfOn, 0.0f); }
    // Report and reset ONLY after the emission pass, which is the last of the
    // two layer calls. Doing it per call made the counters lie: the body pass
    // zeroed them before emission ran, so whichever layer happened to cross the
    // one-second boundary printed its own total as if it were the frame's — and
    // "batches=1" looked identical whether the frame really cost one batch or
    // one hundred split across the other layer. The whole point of this
    // instrument is telling those two cases apart.
    if (layerFilter == 1)
    {
      if (s_perfOn > 0.5f)
      {
        s_perfLog += GetFrameTime();
        if (s_perfLog >= 1.0f)
        {
          s_perfLog = 0.0f;
          int lights = 0;
          VFXLight_GetStats(&lights, NULL);
          TraceLog(LOG_INFO,
                   "PARTICLE perf: live=%d quads=%d batches=%d vfxLights=%d fps=%d",
                   s_activeCount, s_perfQuads, s_perfBatches, lights, GetFPS());
        }
      }
      s_perfBatches = 0;
      s_perfQuads = 0;
    }
  }

  // Second pass: Draw Particle Ribbon Trails
  //
  // The trail inherits the PARTICLE's blend mode. It used to run here with the
  // blend state already torn down above, i.e. always BLEND_ALPHA — so an
  // additive, emissive particle (the F1b blend law's whole point) dragged a
  // trail that did not emit, and the tail read as grey smear over a glowing
  // head. Nothing in the project had noticed because until E5.3 nothing paired
  // trailLength with VFX_BLEND_ADDITIVE.
  int trailBlend = -1;
  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->trailLength > 0 && p->trailHistoryCount >= 2)
    {
      if (layerFilter == 0 && p->blendMode != VFX_BLEND_ALPHA) continue;
      if (layerFilter == 1 && p->blendMode == VFX_BLEND_ALPHA) continue;
      // Ribbon trails carry no volume sheet, so a PREMULTIPLIED head still
      // trails in plain ADDITIVE — the trail is a solid-colour strip, and
      // premultiplied output would need an alpha it does not compute.
      int want = (layerFilter == 0 || p->blendMode == VFX_BLEND_ALPHA) ? BLEND_ALPHA : BLEND_ADDITIVE;
      if (want != trailBlend)
      {
        if (trailBlend >= 0) EndBlendMode();
        BeginBlendMode(want);
        trailBlend = want;
      }
      RibbonPoint trailPoints[PS_TRAIL_MAX_RIBBON_PTS];
      int count = p->trailHistoryCount;
      if (count > p->trailLength) count = p->trailLength;

      // Sample base particle alpha for alpha scaling
      float lifeRatio = p->lifetime / p->maxLifetime;
      float invRatio = 1.0f - lifeRatio;
      Color baseColor = p->colorStart;
      if (p->gradient) {
        baseColor = ColorGradient_Sample(p->gradient, invRatio);
      } else {
        baseColor.a = (unsigned char)((int)p->colorStart.a * lifeRatio + (int)p->colorEnd.a * invRatio);
      }
      if (p->alphaCurve) {
        float mul = SkillCurve_Eval(p->alphaCurve, invRatio);
        int a_val = (int)((float)p->colorStart.a * mul);
        baseColor.a = (unsigned char)(a_val < 0 ? 0 : (a_val > 255 ? 255 : a_val));
      }

      float baseAlpha = (float)baseColor.a / 255.0f;

      // Smoothing: the history is a coarse polyline (8 points, one per
      // trailStepTime), and on a curving path the corners are visible as
      // FACETS — the tail reads as a bent wire, not as a thread of gas. When
      // trailSmooth is on, each segment is subdivided with a Catmull-Rom
      // through the recorded points, which passes exactly through them (no
      // control points to invent) and stays C1 across joints.
      int sub = p->trailSmooth ? PS_TRAIL_SUBDIV : 1;
      int outCount = (count - 1) * sub + 1;
      if (outCount > PS_TRAIL_MAX_RIBBON_PTS) { sub = 1; outCount = count; }

      for (int o = 0; o < outCount; o++)
      {
        float fh = (float)o / (float)sub;      // position in history index space
        int   h  = (int)fh;
        float ft = fh - (float)h;
        if (h > count - 1) { h = count - 1; ft = 0.0f; }

        Vector3 pos;
        if (sub == 1 || ft <= 0.0001f)
        {
          pos = p->trailHistory[h];
        }
        else
        {
          // Endpoints duplicate the terminal point, the standard clamped
          // Catmull-Rom boundary, so the curve does not overshoot at the ends.
          int i0 = (h - 1 < 0) ? 0 : h - 1;
          int i1 = h;
          int i2 = (h + 1 > count - 1) ? count - 1 : h + 1;
          int i3 = (h + 2 > count - 1) ? count - 1 : h + 2;
          pos = PS_CatmullRom(p->trailHistory[i0], p->trailHistory[i1],
                              p->trailHistory[i2], p->trailHistory[i3], ft);
        }

        trailPoints[o].position = pos;

        float ageRatio = fh / (float)(count - 1);
        trailPoints[o].halfWidth = p->radius * p->trailWidthRatio * (1.0f - ageRatio * 0.7f);

        Color tc = ColorLerp(p->trailColorStart, p->trailColorEnd, ageRatio);
        float alphaFade = 1.0f - ageRatio * 0.9f;
        trailPoints[o].tint = ColorAlpha(tc, baseAlpha * alphaFade);
        trailPoints[o].v = ageRatio;
      }

      DrawRibbonStripProfiledEx(
          trailPoints, outCount, texture, camera, RIBBON_CAMERA_FACING,
          (Vector3){0.0f, 1.0f, 0.0f}, p->contrastProfile,
          p->blendMode == VFX_BLEND_ADDITIVE
              ? VFX_CONTRAST_EMISSION
              : VFX_CONTRAST_BODY);
    }
  }
  if (trailBlend >= 0) EndBlendMode();

  rlSetTexture(0);
}

void DrawParticles(Camera3D camera, Texture2D texture)
{
  DrawParticlesLayer(camera, texture, -1);
}

void DrawParticlesBody(Camera3D camera, Texture2D texture)
{
  DrawParticlesLayer(camera, texture, 0);
}

void DrawParticlesEmission(Camera3D camera, Texture2D texture)
{
  DrawParticlesLayer(camera, texture, 1);
}

void UnloadParticleSystem(void) { InitParticleSystem(); }

bool IsParticleSystemActive(void) { return s_activeCount > 0; }

bool ParticleSystem_HasVolumeParticles(void)
{
  for (int i = 0; i < s_activeCount; ++i)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[i]];
    if (p->volumeSheet && p->renderMode != 3 && !p->trailOnly)
      return true;
  }
  return false;
}

bool ParticleSystem_HasAdditiveParticles(void)
{
  for (int i = 0; i < s_activeCount; ++i)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[i]];
    // This gates whether main.c runs the EMISSION pass at all, so a mode
    // missing from the test is not dimmed — it is never drawn. Keep it in step
    // with the layer filter in DrawParticlesLayer: ADDITIVE is the only mode
    // that goes to emission (PREMULTIPLIED is drawn in the body pass, see the
    // note there). If the two ever disagree, particles vanish silently.
    if (p->blendMode != VFX_BLEND_ALPHA && p->renderMode != 3 && !p->trailOnly)
      return true;
  }
  return false;
}

void ParticleSystem_ResetForceFieldRegistry(void) {}

void ParticleSystem_GetStats(int *active, int *max)
{
  *active = s_activeCount;
  *max = MAX_PARTICLES;
}

int ParticleSystem_GetSurfaceSamples(int emitterId, ParticleSurfaceSample *outSamples, int maxSamples)
{
  if (!outSamples || maxSamples <= 0) return 0;
  int count = 0;
  for (int i = 0; i < MAX_PARTICLES && count < maxSamples; ++i) {
    ParticleInternal *p = &g_Particles[i];
    if (!p->active || p->emitterId != emitterId || p->renderMode != 3) continue;
    outSamples[count++] = (ParticleSurfaceSample){ {p->x, p->y, p->z}, p->radius };
  }
  return count;
}

#ifndef PI
#define PI 3.1415926535f
#endif

void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg)
{
  if (cfg == NULL)
    return;

  ParticleRadialBurstConfig localCfg = *cfg;
  ParticleRadialBurstConfig_Unify(&localCfg);
  cfg = &localCfg;

  int count = GetRandomValue(cfg->countMin, cfg->countMax);
  count = (int)((float)count * sizeScale);

  for (int s = 0; s < count; s++)
  {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * cfg->pitchRange;
    float speed = Math_Mix(cfg->speedMin, cfg->speedMax, Random01()) * sizeScale;
    float cosPitch = cosf(pitch);

    ParticleConfig pcfg = {0};
    pcfg.position = origin;
    pcfg.velocity = (Vector3){
        cosf(angle) * speed * cosPitch,
        sinf(pitch) * speed + (cfg->upwardBias * sizeScale),
        sinf(angle) * speed * cosPitch};
    pcfg.radius = Math_Mix(cfg->radiusMin, cfg->radiusMax, Random01()) * sizeScale;
    pcfg.lifetime = Math_Mix(cfg->lifetimeMin, cfg->lifetimeMax, Random01());
    pcfg.colorStart = cfg->colorStart;
    pcfg.colorEnd = cfg->colorEnd;
    pcfg.forceField = cfg->forceField;
    pcfg.gradient = cfg->gradient;

    SpawnParticle(pcfg);
  }
}

void SpawnParticleOnMesh(const struct MeshAdjacency *adj, Matrix transform, ParticleConfig config) {
  if (!adj || adj->count == 0) return;
  Vector3 localPos = MeshAdjacency_SampleEdge(adj);
  Vector3 worldPos = Vector3Transform(localPos, transform);
  config.position = worldPos;
  config.physics.position = worldPos;
  SpawnParticle(config);
}
