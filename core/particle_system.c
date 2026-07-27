#include "particle_system.h"
#include "mesh_adjacency.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/utils_math.h"
#include "core/ribbon_strip.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "core/gfx_quality.h"
#include "core/tuning.h"
#include "environment/environment_system.h"
#include <string.h>
#include <math.h>

#define MAX_PARTICLES 2000

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

  // --- COLD DATA (Con trỏ, ít khi rẽ nhánh) ---
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
  unsigned int texId;   // 0 = use the batch default passed to DrawParticles
  int blendMode;        // VFX_BlendMode — see the blend law in vfx_config.h
  int unlit;            // 1 = emissive, skip the lighting multiply
  float emissiveBoost;  // >1 = HDR headroom for a glowing core (see vfx_config.h)
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

  p->forceField = config.forceField;
  p->gradient = config.gradient;
  p->spriteAnim = config.spriteAnim;
  p->texId = config.render.texture.id;
  p->blendMode = config.render.blendMode;
  p->unlit = config.render.unlit;
  p->emissiveBoost = (config.render.emissiveBoost > 0.0f) ? config.render.emissiveBoost : 1.0f;
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
      if (p->trailHistoryTimer >= 0.015f || p->trailHistoryCount == 0)
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

  if (s_lightingStrength <= 0.0f)
    return;
  // Per-fragment lighting on every particle is real fill-rate; the Mali devices
  // are the constraint (ENGINE_LANDMINES.md). LOW/UNLIT keep the cheap path.
  if (GfxQuality_Get() <= GFX_LOW)
    return;

  if (!s_litShaderTried)
  {
    s_litShaderTried = true;
    s_litShader = ResourceManager_LoadShader("core/shaders/particle_lit.vs",
                                             "core/shaders/particle_lit.fs");
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
      s_locAtlasGrid       = GetShaderLocation(s_litShader, "u_atlasGrid");
      s_locEmissiveBoost   = GetShaderLocation(s_litShader, "u_emissiveBoost");
    }
    else
    {
      // Never fail silently — a skipped shader that logs nothing has cost this
      // project a full debugging session before (rlvk_shaderc.inl:1129).
      TraceLog(LOG_WARNING,
               "PARTICLE: particle_lit shader failed to load — particles stay unlit");
    }
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
  { // default: not an atlas. Flipped per batch in the draw loop.
    float grid[2] = {1.0f, 1.0f};
    if (s_locAtlasGrid >= 0) SetShaderValue(s_litShader, s_locAtlasGrid, grid, SHADER_UNIFORM_VEC2);
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
  EndShaderMode();
  s_litActive = false;
}

void DrawParticles(Camera3D camera, Texture2D texture)
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

  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];

    unsigned int want = p->texId ? p->texId : texture.id;
    int wantGridC = (p->spriteAnim ? p->spriteAnim->cols : 1);
    int wantGridR = (p->spriteAnim ? p->spriteAnim->rows : 1);
    // Global multiplier on top of the per-particle value, so the whole look can
    // be dialled without touching every call site.
    float wantBoost = p->emissiveBoost * s_emissiveBoost;
    if (want != curTex || p->blendMode != curBlend || p->unlit != curUnlit ||
        wantGridC != curGridC || wantGridR != curGridR || wantBoost != curBoost)
    {
      if (curTex != 0xFFFFFFFFu) rlEnd();
      if (p->blendMode != curBlend)
      {
        // Blend state must be flushed either side or it leaks across the batch
        // boundary — ENGINE_LANDMINES.md §1, the raylib batching hazard.
        rlDrawRenderBatchActive();
        if (curBlend >= 0) EndBlendMode();
        BeginBlendMode(p->blendMode == VFX_BLEND_ADDITIVE ? BLEND_ADDITIVE : BLEND_ALPHA);
        rlDrawRenderBatchActive();
        curBlend = p->blendMode;
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
        ParticleLighting_SetEmissive(p->unlit ? wantBoost : 1.0f);
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
    Rectangle uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    Rectangle uvNext = uv;
    float     fbBlend = 0.0f;
    if (p->spriteAnim)
    {
      float age = p->maxLifetime - p->lifetime;
      // Đợt E / E4 — cross-faded flipbook. Snapping to whole frames makes an
      // authored sheet read as the sprites flipping back and forth (32 fps atlas
      // against a 60 fps render, ~2 render frames per atlas frame, then a jump to
      // a different simulation state). Blending the two adjacent frames removes
      // the jump entirely.
      uv = SpriteAnim_CalculateUVBlend(p->spriteAnim, age, &uvNext, &fbBlend);
      // Kill switch + A/B. Cross-fading emits TWO quads per particle, so this is
      // also the lever if the vertex cost ever matters on a weak device.
      // Registered lazily on first use, never from an Init (docs/LANDMINES.md).
      {
        static float s_fbBlendOn = 1.0f;
        static bool  s_fbBlendReg = false;
        if (!s_fbBlendReg) { s_fbBlendReg = true;
          Tuning_RegisterFloat("particle_fb_blend", &s_fbBlendOn, 1.0f); }
        if (s_fbBlendOn <= 0.5f) fbBlend = 0.0f;
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
    #define PS_EMIT_QUAD(_uv, _a)                                                  \
      do {                                                                         \
        if ((_a) > 0) {                                                            \
          rlColor4ub(c.r, c.g, c.b, (unsigned char)(_a));                           \
          rlTexCoord2f((_uv).x, (_uv).y);                                          \
          rlVertex3f(p->x + rx - ux, p->y + ry - uy, p->z + rz - uz);              \
          rlTexCoord2f((_uv).x, (_uv).y + (_uv).height);                           \
          rlVertex3f(p->x + rx + ux, p->y + ry + uy, p->z + rz + uz);              \
          rlTexCoord2f((_uv).x + (_uv).width, (_uv).y + (_uv).height);             \
          rlVertex3f(p->x - rx + ux, p->y - ry + uy, p->z - rz + uz);              \
          rlTexCoord2f((_uv).x + (_uv).width, (_uv).y);                            \
          rlVertex3f(p->x - rx - ux, p->y - ry - uy, p->z - rz - uz);              \
        }                                                                          \
      } while (0)

    if (fbBlend > 0.001f)
    {
      PS_EMIT_QUAD(uv,     (float)c.a * (1.0f - fbBlend));
      PS_EMIT_QUAD(uvNext, (float)c.a * fbBlend);
    }
    else
    {
      PS_EMIT_QUAD(uv, (float)c.a);
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

  // Second pass: Draw Particle Ribbon Trails
  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->trailLength > 0 && p->trailHistoryCount >= 2)
    {
      RibbonPoint trailPoints[8];
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

      for (int h = 0; h < count; h++)
      {
        trailPoints[h].position = p->trailHistory[h];

        float ageRatio = (float)h / (float)(count - 1);
        trailPoints[h].halfWidth = p->radius * p->trailWidthRatio * (1.0f - ageRatio * 0.7f);

        Color tc = ColorLerp(p->trailColorStart, p->trailColorEnd, ageRatio);
        float alphaFade = 1.0f - ageRatio * 0.9f;
        trailPoints[h].tint = ColorAlpha(tc, baseAlpha * alphaFade);
        trailPoints[h].v = ageRatio;
      }

      DrawRibbonStrip(trailPoints, count, texture, camera);
    }
  }

  rlSetTexture(0);
}

void UnloadParticleSystem(void) { InitParticleSystem(); }

bool IsParticleSystemActive(void) { return s_activeCount > 0; }

void ParticleSystem_ResetForceFieldRegistry(void) {}

void ParticleSystem_GetStats(int *active, int *max)
{
  *active = s_activeCount;
  *max = MAX_PARTICLES;
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