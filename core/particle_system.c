#include "particle_system.h"
#include "mesh_adjacency.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/utils_math.h"
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

  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);

  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];

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

    // Xoay hạt quanh trục hướng camera (Billboard-space 2D Rotation)
    if (p->rotation != 0.0f)
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
    if (p->spriteAnim)
    {
      float age = p->maxLifetime - p->lifetime;
      uv = SpriteAnim_CalculateUV(p->spriteAnim, age, NULL);
    }

    rlColor4ub(c.r, c.g, c.b, c.a);

    // TL
    rlTexCoord2f(uv.x, uv.y);
    rlVertex3f(p->x + rx - ux, p->y + ry - uy, p->z + rz - uz);
    // BL
    rlTexCoord2f(uv.x, uv.y + uv.height);
    rlVertex3f(p->x + rx + ux, p->y + ry + uy, p->z + rz + uz);
    // BR
    rlTexCoord2f(uv.x + uv.width, uv.y + uv.height);
    rlVertex3f(p->x - rx + ux, p->y - ry + uy, p->z - rz + uz);
    // TR
    rlTexCoord2f(uv.x + uv.width, uv.y);
    rlVertex3f(p->x - rx - ux, p->y - ry - uy, p->z - rz - uz);
  }

  rlEnd();
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