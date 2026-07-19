#include "core/trail_system.h"
#include "core/force_field.h"
#include "core/composition/visual_composer.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

static void KillTrailInternal(int id);
static TrailEntity trailPool[MAX_TRAIL_PARTICLES];
static int freeListHead = 0;
static int activeCount = 0;
static int s_activeIds[MAX_TRAIL_PARTICLES];
static int s_slotListIndex[MAX_TRAIL_PARTICLES];

typedef struct
{
  Vector3 right;
  Vector3 up;
} TrailCameraBasis;

static Texture2D s_globalTrailTex = {0};
void TrailSystem_SetGlobalTexture(Texture2D tex) { s_globalTrailTex = tex; }
static Shader defaultShader;

#define TRAIL_SHADER_CACHE_SIZE 16
static unsigned int shaderCacheIds[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheTimeLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheCount = 0;

static int GetCachedTimeLoc(Shader shader)
{
  for (int i = 0; i < shaderCacheCount; i++)
  {
    if (shaderCacheIds[i] == shader.id)
      return shaderCacheTimeLocs[i];
  }
  int loc = GetShaderLocation(shader, "u_time");
  if (shaderCacheCount < TRAIL_SHADER_CACHE_SIZE)
  {
    shaderCacheIds[shaderCacheCount] = shader.id;
    shaderCacheTimeLocs[shaderCacheCount] = loc;
    shaderCacheCount++;
  }
  return loc;
}

static RibbonPoint scratchOuter[TRAIL_HISTORY_COUNT];
static RibbonPoint scratchInner[TRAIL_HISTORY_COUNT];
static Vector3 scratchNodePrevPos[TRAIL_HISTORY_COUNT];

#define WISP_CONSTRAINT_ITERS 2

static inline Shader ResolveShader(const TrailEntity *t)
{
  return (t->shader.id != 0) ? t->shader : defaultShader;
}

static float SmoothStepC(float edge0, float edge1, float x)
{
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f)
    return 0.0f;
  if (t > 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

static inline float ComputeWispStyleTaper(float segRatio)
{
  return SmoothStepC(0.0f, TRAIL_WISP_HEAD_TAPER_EDGE, segRatio) *
         SmoothStepC(1.0f, TRAIL_WISP_TAIL_TAPER_EDGE, 1.0f - segRatio);
}

static float ComputeWidthEnvelope(const TrailEntity *t, float segRatio, float time)
{
  if (t->widthCurve)
  {
    return SkillCurve_Eval(t->widthCurve, segRatio);
  }
  switch (t->widthEnvelope)
  {
  case TRAIL_WIDTH_ENVELOPE_TAPER_TAIL:
    return powf(segRatio, 1.2f);
  case TRAIL_WIDTH_ENVELOPE_TAPER_BOTH:
    return powf(sinf(segRatio * 3.14159265f), 0.6f);
  case TRAIL_WIDTH_ENVELOPE_PULSE:
    return 1.0f + 0.25f * sinf(segRatio * 12.0f - time * 10.0f);
  case TRAIL_WIDTH_ENVELOPE_UNIFORM:
  default:
    return 1.0f;
  }
}

static inline Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
  float t2 = t * t;
  float t3 = t2 * t;

  float f0 = -0.5f * t3 + t2 - 0.5f * t;
  float f1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
  float f2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
  float f3 = 0.5f * t3 - 0.5f * t2;

  return (Vector3){
      p0.x * f0 + p1.x * f1 + p2.x * f2 + p3.x * f3,
      p0.y * f0 + p1.y * f1 + p2.y * f2 + p3.y * f3,
      p0.z * f0 + p1.z * f1 + p2.z * f2 + p3.z * f3};
}

static inline int GetHistoryNodeIndex(const TrailEntity *t, int i)
{
  if (t->type == TRAIL_TYPE_WISP)
  {
    return t->historyCount - 1 - i;
  }
  else
  {
    return (t->historyHead - (t->historyCount - 1 - i) + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
  }
}

static Vector3 GetInterpolatedPosition(const TrailEntity *t, float segRatio)
{
  if (t->historyCount < 1)
    return (Vector3){0, 0, 0};
  if (t->historyCount == 1)
    return t->history[GetHistoryNodeIndex(t, 0)];

  float idx = segRatio * (t->historyCount - 1);
  int i = (int)floorf(idx);
  float f = idx - (float)i;

  int N = t->historyCount;
  int p0 = i - 1; if (p0 < 0) p0 = 0;
  int p1 = i; if (p1 >= N) p1 = N - 1;
  int p2 = i + 1; if (p2 >= N) p2 = N - 1;
  int p3 = i + 2; if (p3 >= N) p3 = N - 1;

  int idxP0 = GetHistoryNodeIndex(t, p0);
  int idxP1 = GetHistoryNodeIndex(t, p1);
  int idxP2 = GetHistoryNodeIndex(t, p2);
  int idxP3 = GetHistoryNodeIndex(t, p3);

  return CatmullRom(t->history[idxP0], t->history[idxP1], t->history[idxP2], t->history[idxP3], f);
}

// Đã tối ưu lượng giác và LOẠI BỎ lệnh rlSetTexture(0) để giữ Batching
static void DrawCameraFacingQuad(const TrailCameraBasis *cam, Vector3 center,
                                 float width, float height, float rotation,
                                 Color tint, Texture2D tex, Rectangle uvRect)
{
  Vector3 rVec = Vector3Scale(cam->right, width * 0.5f);
  Vector3 uVec = Vector3Scale(cam->up, height * 0.5f);

  if (rotation != 0.0f)
  {
    float cosR = cosf(rotation);
    float sinR = sinf(rotation);
    Vector3 tR = rVec;
    rVec = (Vector3){rVec.x * cosR + uVec.x * sinR, rVec.y * cosR + uVec.y * sinR, rVec.z * cosR + uVec.z * sinR};
    uVec = (Vector3){uVec.x * cosR - tR.x * sinR, uVec.y * cosR - tR.y * sinR, uVec.z * cosR - tR.z * sinR};
  }

  Vector3 tl = {center.x - rVec.x + uVec.x, center.y - rVec.y + uVec.y, center.z - rVec.z + uVec.z};
  Vector3 tr = {center.x + rVec.x + uVec.x, center.y + rVec.y + uVec.y, center.z + rVec.z + uVec.z};
  Vector3 bl = {center.x - rVec.x - uVec.x, center.y - rVec.y - uVec.y, center.z - rVec.z - uVec.z};
  Vector3 br = {center.x + rVec.x - uVec.x, center.y + rVec.y - uVec.y, center.z + rVec.z - uVec.z};

  if (tex.id > 0)
    rlSetTexture(tex.id);

  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);

  rlTexCoord2f(uvRect.x, uvRect.y);
  rlVertex3f(tl.x, tl.y, tl.z);
  rlTexCoord2f(uvRect.x, uvRect.y + uvRect.height);
  rlVertex3f(bl.x, bl.y, bl.z);
  rlTexCoord2f(uvRect.x + uvRect.width, uvRect.y + uvRect.height);
  rlVertex3f(br.x, br.y, br.z);
  rlTexCoord2f(uvRect.x + uvRect.width, uvRect.y);
  rlVertex3f(tr.x, tr.y, tr.z);

  rlEnd();
  // KHÔNG gọi rlSetTexture(0) ở đây! Để rlgl tự quản lý state.
}

// Khai triển toán học trực tiếp thay vì gọi struct Vector3 liên tục (giảm overhead Push/Pop RAM)
static inline void ConstrainRibbonSegment(Vector3 *a, Vector3 *b, float restLen, bool pinnedA)
{
  if (restLen <= 1e-6f)
    return;

  float dx = b->x - a->x;
  float dy = b->y - a->y;
  float dz = b->z - a->z;
  float dist2 = dx * dx + dy * dy + dz * dz;
  float restLen2 = restLen * restLen;

  if (fabsf(dist2 - restLen2) < restLen2 * 1e-8f || dist2 < 1e-10f)
    return;

  float dist = sqrtf(dist2);
  float invDist = 1.0f / dist;
  float err = dist - restLen;

  float dirX = dx * invDist * err;
  float dirY = dy * invDist * err;
  float dirZ = dz * invDist * err;

  if (pinnedA)
  {
    b->x -= dirX;
    b->y -= dirY;
    b->z -= dirZ;
  }
  else
  {
    float hX = dirX * 0.5f, hY = dirY * 0.5f, hZ = dirZ * 0.5f;
    a->x += hX;
    a->y += hY;
    a->z += hZ;
    b->x -= hX;
    b->y -= hY;
    b->z -= hZ;
  }
}

static inline void GrowHistoryTowardMaxNodes(TrailEntity *t)
{
  int maxNodes = (t->trailLength > 0.0f) ? (int)t->trailLength : TRAIL_HISTORY_COUNT;
  if (maxNodes > TRAIL_HISTORY_COUNT)
    maxNodes = TRAIL_HISTORY_COUNT;
  if (maxNodes < 1)
    maxNodes = 1;

  if (t->historyCount < maxNodes)
    t->historyCount++;
  else if (t->historyCount > maxNodes)
    t->historyCount = maxNodes;
}

static void UpdateProjectilePhysics(int id, TrailEntity *t, float dt, float time)
{
  float velLen = Vector3Length(t->velocity);
  Vector3 dir = (velLen > 1e-6f) ? (Vector3){t->velocity.x / velLen, t->velocity.y / velLen, t->velocity.z / velLen}
                                 : (Vector3){0.0f, 0.0f, 1.0f};

  Vector3 spawnPos = (Vector3){
      t->position.x - dir.x * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL),
      t->position.y - dir.y * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL),
      t->position.z - dir.z * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL)};

  bool shouldInsert = true;
  if (t->minVertexDistance > 0.0f && t->historyCount > 0)
  {
    Vector3 lastNode = t->history[t->historyHead];
    float distSqr = (spawnPos.x - lastNode.x) * (spawnPos.x - lastNode.x) +
                    (spawnPos.y - lastNode.y) * (spawnPos.y - lastNode.y) +
                    (spawnPos.z - lastNode.z) * (spawnPos.z - lastNode.z);
    if (distSqr < t->minVertexDistance * t->minVertexDistance)
    {
      shouldInsert = false;
    }
  }

  if (shouldInsert)
  {
    t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;
    GrowHistoryTowardMaxNodes(t);
  }

  t->history[t->historyHead] = spawnPos;
  t->wobblePhase += dt * TRAIL_PROJECTILE_WOBBLE_FREQ;
  Vector3 posBeforeMove = t->position;

  Vector3 toTarget = Vector3Subtract(t->target, t->position);
  float distSqr = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;

  if (distSqr > TRAIL_PROJECTILE_RETARGET_DIST_SQR)
  {
    float curveRange = (t->curveRangeOverride > 0.0f) ? t->curveRangeOverride : TRAIL_PROJECTILE_CURVE_RANGE;
    float wobbleAmp = (t->wobbleAmplitudeOverride > 0.0f) ? t->wobbleAmplitudeOverride : TRAIL_PROJECTILE_WOBBLE_AMPLITUDE;

    float distToTarget = sqrtf(distSqr);
    float invDist = 1.0f / distToTarget;
    Vector3 desiredDir = {toTarget.x * invDist, toTarget.y * invDist, toTarget.z * invDist};

    float newSpeed = fminf(velLen + TRAIL_PROJECTILE_ACCEL_RATE * dt, TRAIL_PROJECTILE_MAX_SPEED);
    float curveStrength = fminf(distToTarget / curveRange, 1.0f);

    Vector3 perpDir = {-desiredDir.z, 0.0f, desiredDir.x};
    float wobble = sinf(t->wobblePhase) * wobbleAmp * curveStrength * dt;

    Vector3 desiredVel = {
        desiredDir.x * newSpeed + perpDir.x * wobble,
        desiredDir.y * newSpeed + perpDir.y * wobble,
        desiredDir.z * newSpeed + perpDir.z * wobble};

    t->velocity = Vector3Lerp(t->velocity, desiredVel, dt * TRAIL_PROJECTILE_STEER_LERP_RATE);
  }

  const bool windActive = WindZone_IsActive();
  if (t->forceField || windActive)
  {
    Vector3 acc = (Vector3){0, 0, 0};
    if (t->forceField)
    {
      acc = ForceField_Evaluate(t->forceField, t->position, t->velocity, time, (Vector3){0}, (Vector3){0});
    }
    if (windActive)
    {
      Vector3 windAcc = WindZone_Evaluate(t->position, t->velocity, time);
      acc.x += windAcc.x; acc.y += windAcc.y; acc.z += windAcc.z;
    }
    t->velocity.x += acc.x * dt;
    t->velocity.y += acc.y * dt;
    t->velocity.z += acc.z * dt;

    if (t->forceField)
    {
      float viscDamp = ForceField_GetViscosityDamping(t->forceField, dt);
      t->velocity.x *= viscDamp;
      t->velocity.y *= viscDamp;
      t->velocity.z *= viscDamp;
    }
  }

  t->position.x += t->velocity.x * dt;
  t->position.y += t->velocity.y * dt;
  t->position.z += t->velocity.z * dt;

  Vector3 moveDelta = Vector3Subtract(t->position, posBeforeMove);
  float moveLenSqr = moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y + moveDelta.z * moveDelta.z;
  Vector3 toTargetFromStart = Vector3Subtract(t->target, posBeforeMove);

  float closestDistSqr;
  if (moveLenSqr < 1e-8f)
  {
    closestDistSqr = Vector3LengthSqr(Vector3Subtract(t->target, t->position));
  }
  else
  {
    float proj = Vector3DotProduct(toTargetFromStart, moveDelta) / moveLenSqr;
    proj = fmaxf(0.0f, fminf(1.0f, proj));
    Vector3 closestPoint = {posBeforeMove.x + moveDelta.x * proj, posBeforeMove.y + moveDelta.y * proj, posBeforeMove.z + moveDelta.z * proj};
    closestDistSqr = Vector3LengthSqr(Vector3Subtract(t->target, closestPoint));
  }

  bool isHit = (closestDistSqr < TRAIL_PROJECTILE_HIT_DIST_SQR);
  if (!isHit && t->collisionCheck != NULL)
  {
    isHit = t->collisionCheck(id, t->position);
  }

  if (isHit)
  {
    t->type = TRAIL_TYPE_FOLLOWER;
    t->attachedTransform = NULL;
    t->timeSinceLastFollowerUpdate = 0.0f;
    t->fadeAccumulator = 0.0f;
  }
}

static void UpdateWispPhysics(TrailEntity *t, float dt, float time)
{
  const bool windActive = WindZone_IsActive();
  if ((!t->forceField && !windActive) || t->historyCount < 2 || t->nodeRestLen <= 0.0f)
    return;
  float viscDamp = t->forceField ? ForceField_GetViscosityDamping(t->forceField, dt) : 1.0f;
  float restLen = t->nodeRestLen;

  for (int h = 0; h < t->historyCount; h++)
  {
    Vector3 acc = (Vector3){0, 0, 0};
    if (t->forceField)
    {
      acc = ForceField_Evaluate(t->forceField, t->history[h], t->nodeVelocity[h], time, (Vector3){0}, (Vector3){0});
    }
    if (windActive)
    {
      Vector3 windAcc = WindZone_Evaluate(t->history[h], t->nodeVelocity[h], time);
      acc.x += windAcc.x; acc.y += windAcc.y; acc.z += windAcc.z;
    }
    t->nodeVelocity[h] = (Vector3){
        (t->nodeVelocity[h].x + acc.x * dt) * viscDamp,
        (t->nodeVelocity[h].y + acc.y * dt) * viscDamp,
        (t->nodeVelocity[h].z + acc.z * dt) * viscDamp};
    scratchNodePrevPos[h] = t->history[h];
    t->history[h] = (Vector3){
        t->history[h].x + t->nodeVelocity[h].x * dt,
        t->history[h].y + t->nodeVelocity[h].y * dt,
        t->history[h].z + t->nodeVelocity[h].z * dt};
  }

  for (int iter = 0; iter < WISP_CONSTRAINT_ITERS; iter++)
  {
    for (int h = 1; h < t->historyCount; h++)
    {
      ConstrainRibbonSegment(&t->history[h - 1], &t->history[h], restLen, false);
    }
    for (int h = t->historyCount - 1; h >= 1; h--)
    {
      ConstrainRibbonSegment(&t->history[h - 1], &t->history[h], restLen, false);
    }
  }

  if (dt > 1e-7f)
  {
    float invDt = 1.0f / dt;
    for (int h = 0; h < t->historyCount; h++)
    {
      t->nodeVelocity[h] = (Vector3){
          (t->history[h].x - scratchNodePrevPos[h].x) * invDt,
          (t->history[h].y - scratchNodePrevPos[h].y) * invDt,
          (t->history[h].z - scratchNodePrevPos[h].z) * invDt};
    }
  }
  t->position = t->history[0];
}

static void UpdateFollowerPhysics(int i, TrailEntity *t, float dt, float time)
{
  t->timeSinceLastFollowerUpdate += dt;
  if (t->timeSinceLastFollowerUpdate > TRAIL_FOLLOWER_IDLE_FADE_TIME)
  {
    t->fadeAccumulator += TRAIL_FOLLOWER_FADE_RATE_PER_SEC * dt;
    int fadeCount = (int)t->fadeAccumulator;
    if (fadeCount > 0)
    {
      t->historyCount -= fadeCount;
      t->fadeAccumulator -= (float)fadeCount;
    }
    if (t->historyCount <= 0)
    {
      KillTrailInternal(i);
      return;
    }
  }

  const bool windActive = WindZone_IsActive();
  if ((!t->forceField && !windActive) || t->historyCount < 2)
    return;
  float viscDamp = t->forceField ? ForceField_GetViscosityDamping(t->forceField, dt) : 1.0f;

  for (int h = 1; h < t->historyCount; h++)
  {
    int idx = (t->historyHead - h + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
    Vector3 acc = (Vector3){0, 0, 0};
    if (t->forceField)
    {
      acc = ForceField_Evaluate(t->forceField, t->history[idx], t->nodeVelocity[idx], time, t->axisOrigin, t->axisDir);
    }
    if (windActive)
    {
      Vector3 windAcc = WindZone_Evaluate(t->history[idx], t->nodeVelocity[idx], time);
      acc.x += windAcc.x; acc.y += windAcc.y; acc.z += windAcc.z;
    }

    t->nodeVelocity[idx] = (Vector3){
        (t->nodeVelocity[idx].x + acc.x * dt) * viscDamp,
        (t->nodeVelocity[idx].y + acc.y * dt) * viscDamp,
        (t->nodeVelocity[idx].z + acc.z * dt) * viscDamp};
    t->history[idx] = (Vector3){
        t->history[idx].x + t->nodeVelocity[idx].x * dt,
        t->history[idx].y + t->nodeVelocity[idx].y * dt,
        t->history[idx].z + t->nodeVelocity[idx].z * dt};
  }
}

// Khởi tạo, Spawn, Destroy giữ nguyên (lược bỏ nội dung trung gian do không gặp nút thắt lớn ở đây)
void InitTrailSystem(Shader defaultShaderIn)
{
  defaultShader = defaultShaderIn;
  shaderCacheCount = 0;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++)
  {
    trailPool[i].active = false;
    trailPool[i].nextFree = i + 1;
    s_slotListIndex[i] = -1;
  }
  freeListHead = 0;
  activeCount = 0;
}

TrailEntity *GetTrail(int id) { return (id < 0 || id >= MAX_TRAIL_PARTICLES) ? NULL : &trailPool[id]; }

static void KillTrailInternal(int id)
{
  if (trailPool[id].onDeath)
    trailPool[id].onDeath(trailPool[id].position, trailPool[id].scale);
  trailPool[id].active = false;
  int listIdx = s_slotListIndex[id];
  int lastId = s_activeIds[activeCount - 1];
  s_activeIds[listIdx] = lastId;
  s_slotListIndex[lastId] = listIdx;
  s_slotListIndex[id] = -1;
  activeCount--;
  trailPool[id].nextFree = freeListHead;
  freeListHead = id;
}

void KillTrail(int id)
{
  if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active)
    KillTrailInternal(id);
}
int GetActiveTrailCount(void) { return activeCount; }

static bool EvictLowestPriorityTrail(VFXPriority incomingPriority)
{
  int evictIdx = -1;
  VFXPriority evictPriority = VFX_PRIORITY_HIGH_ULTIMATE;
  float evictLifetime = 999999.0f;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++)
  {
    if (!trailPool[i].active)
      continue;
    if (evictIdx == -1 || trailPool[i].priority < evictPriority ||
        (trailPool[i].priority == evictPriority && trailPool[i].lifetime < evictLifetime))
    {
      evictIdx = i;
      evictPriority = trailPool[i].priority;
      evictLifetime = trailPool[i].lifetime;
    }
  }
  if (evictIdx == -1 || evictPriority > incomingPriority)
    return false;
  KillTrailInternal(evictIdx);
  return true;
}

int SpawnTrailEntity(TrailConfig config)
{
  TrailConfig_Unify(&config);
  if (freeListHead >= MAX_TRAIL_PARTICLES)
    if (!EvictLowestPriorityTrail(config.priority))
      return -1;

  int index = freeListHead;
  freeListHead = trailPool[index].nextFree;
  TrailEntity *t = &trailPool[index];

  // Assign fields...
  t->type = config.type;
  t->position = config.pos;
  t->velocity = config.vel;
  t->target = config.target;
  t->length = config.len;
  t->thickness = config.thick;
  t->trailLength = config.trailLength;
  t->lifetime = config.life;
  t->maxLifetime = config.life;
  t->active = true;
  t->angle = config.initialAngle;
  t->wobblePhase = config.wobblePhase;
  t->scale = config.scale;
  t->sprite = config.tex;
  t->shader = config.shader;
  t->tint = config.tint;
  t->onUpdate = config.onUpdate;
  t->onDeath = config.onDeath;
  t->ownerTag = config.ownerTag;
  t->wobbleAmplitudeOverride = config.wobbleAmplitudeOverride;
  t->curveRangeOverride = config.curveRangeOverride;
  t->forceField = config.forceField;
  t->gradient = config.gradient;
  t->spriteAnim = config.spriteAnim;
  t->priority = config.priority;
  t->orbitRadius = config.orbitRadius;
  t->orbitSpeed = config.orbitSpeed;
  t->orbitAxis = config.orbitAxis;
  t->orbitPhase = config.orbitPhase;
  t->blendMode = config.blendMode;
  t->timeSinceLastFollowerUpdate = 0.0f;
  t->fadeAccumulator = 0.0f;
  t->historyHead = 0;
  t->driftVelocity = (Vector3){0, 0, 0};
  t->axisOrigin = (Vector3){0, 0, 0};
  t->axisDir = (Vector3){0, 0, 0};
  t->attachedTransform = NULL;
  t->attachLocalOffset = (Vector3){0, 0, 0};

  t->collisionCheck = config.collisionCheck;
  t->uvTiling = (config.uvTiling != 0.0f) ? config.uvTiling : 1.0f;
  t->uvScrollSpeed = config.uvScrollSpeed;
  t->uvScrollOffset = 0.0f;
  t->minVertexDistance = config.minVertexDistance;
  t->widthEnvelope = config.widthEnvelope;
  t->smoothSpline = config.smoothSpline;
  t->disableInnerCore = config.disableInnerCore;
  t->useCustomBlendMode = config.useCustomBlendMode;
  t->widthCurve = config.widthCurve;
  t->alphaCurve = config.alphaCurve;
  t->distortionStrength = config.distortionStrength;
  t->distortionSpeed = (config.distortionSpeed != 0.0f) ? config.distortionSpeed : 1.0f;

  for (int h = 0; h < TRAIL_HISTORY_COUNT; h++)
    t->nodeVelocity[h] = (Vector3){0, 0, 0};

  if (config.type == TRAIL_TYPE_WISP)
  {
    int maxNodes = (config.trailLength > 0.0f) ? (int)config.trailLength : TRAIL_HISTORY_COUNT;
    if (maxNodes > TRAIL_HISTORY_COUNT)
      maxNodes = TRAIL_HISTORY_COUNT;
    if (maxNodes < 2)
      maxNodes = 2;
    t->historyCount = maxNodes;
    t->nodeRestLen = (maxNodes > 1 && config.len > 0.0f) ? config.len / (float)(maxNodes - 1) : 0.0f;
    Vector3 strandDir = (Vector3LengthSqr(config.target) > 1e-8f) ? Vector3Normalize(config.target) : (Vector3){0, 0, 1};
    for (int h = 0; h < maxNodes; h++)
    {
      float u = (maxNodes > 1) ? (float)h / (float)(maxNodes - 1) : 0.0f;
      t->history[h] = (Vector3){config.pos.x + strandDir.x * u * config.len, config.pos.y + strandDir.y * u * config.len, config.pos.z + strandDir.z * u * config.len};
      t->nodeVelocity[h] = config.vel;
    }
  }
  else if (config.type == TRAIL_TYPE_FOLLOWER)
  {
    t->historyCount = 0;
    t->nodeRestLen = 0.0f;
  }
  else
  {
    t->historyCount = 0;
    t->nodeRestLen = 0.0f;
    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++)
      t->history[h] = config.pos;
  }

  activeCount++;
  s_slotListIndex[index] = activeCount - 1;
  s_activeIds[activeCount - 1] = index;
  return index;
}

void UpdateFollowerPosition(int id, Vector3 newTipPos)
{
  if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active || trailPool[id].type != TRAIL_TYPE_FOLLOWER)
    return;
  TrailEntity *t = &trailPool[id];

  bool shouldInsert = true;
  if (t->minVertexDistance > 0.0f && t->historyCount > 0)
  {
    Vector3 lastNode = t->history[t->historyHead];
    float distSqr = (newTipPos.x - lastNode.x) * (newTipPos.x - lastNode.x) +
                    (newTipPos.y - lastNode.y) * (newTipPos.y - lastNode.y) +
                    (newTipPos.z - lastNode.z) * (newTipPos.z - lastNode.z);
    if (distSqr < t->minVertexDistance * t->minVertexDistance)
    {
      shouldInsert = false;
    }
  }

  if (shouldInsert)
  {
    t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;
    GrowHistoryTowardMaxNodes(t);
  }

  t->history[t->historyHead] = newTipPos;
  t->nodeVelocity[t->historyHead] = (Vector3){0, 0, 0};
  t->position = newTipPos;
  t->timeSinceLastFollowerUpdate = 0.0f;
  t->fadeAccumulator = 0.0f;
}

void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir)
{
  if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
  {
    trailPool[id].axisOrigin = axisOrigin;
    trailPool[id].axisDir = axisDir;
  }
}
void Trail_AttachToTransform(int id, const Matrix *targetTransform, Vector3 localOffset)
{
  if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
  {
    trailPool[id].attachedTransform = targetTransform;
    trailPool[id].attachLocalOffset = localOffset;
  }
}
void Trail_SetFollowerOrbit(int id, float radius, float speed, Vector3 axis, float phase)
{
  if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
  {
    trailPool[id].orbitRadius = radius;
    trailPool[id].orbitSpeed = speed;
    trailPool[id].orbitAxis = axis;
    trailPool[id].orbitPhase = phase;
  }
}

// Gom chung 2 vòng lặp Update thành 1 để tối ưu Cache L1/L2
void UpdateTrailSystem(float dt)
{
  float time = (float)GetTime();

  for (int a = 0; a < activeCount;)
  {
    int i = s_activeIds[a];
    TrailEntity *t = &trailPool[i];

    t->lifetime -= dt;
    if (t->lifetime <= 0.0f)
    {
      KillTrailInternal(i);
      continue;
    }

    // Accumulate UV scroll offset
    t->uvScrollOffset += t->uvScrollSpeed * dt;

    if (t->type == TRAIL_TYPE_FOLLOWER && t->attachedTransform != NULL)
    {
      Vector3 localPos = t->attachLocalOffset;
      if (t->orbitRadius > 0.0f)
      {
        t->orbitPhase += t->orbitSpeed * dt;
        Vector3 axis = Vector3Normalize(t->orbitAxis);
        if (Vector3LengthSqr(axis) > 0.0f)
        {
          Quaternion q = QuaternionFromAxisAngle(axis, t->orbitPhase);
          Vector3 arbitrary = (fabsf(axis.x) > 0.9f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
          Vector3 ortho = Vector3Normalize(Vector3CrossProduct(axis, arbitrary));
          Vector3 rotated = Vector3RotateByQuaternion(ortho, q);
          localPos = (Vector3){localPos.x + rotated.x * t->orbitRadius, localPos.y + rotated.y * t->orbitRadius, localPos.z + rotated.z * t->orbitRadius};
        }
      }
      UpdateFollowerPosition(i, Vector3Transform(localPos, *t->attachedTransform));
    }

    switch (t->type)
    {
    case TRAIL_TYPE_PROJECTILE:
      UpdateProjectilePhysics(i, t, dt, time);
      break;
    case TRAIL_TYPE_WISP:
      UpdateWispPhysics(t, dt, time);
      break;
    case TRAIL_TYPE_PORTAL:
      t->angle += TRAIL_PORTAL_SPIN_DEG_PER_SEC * dt;
      break;
    case TRAIL_TYPE_FOLLOWER:
      UpdateFollowerPhysics(i, t, dt, time);
      break;
    }

    if (t->active && t->onUpdate)
      t->onUpdate(i, dt);
    a++;
  }
}

static void DrawTrailGeometry(TrailEntity *t, Camera3D camera, const TrailCameraBasis *camBasis, float time)
{
  float lifeRatio = t->lifetime / t->maxLifetime;
  Color c = t->tint;

  if (t->type == TRAIL_TYPE_PROJECTILE)
  {
    if (t->historyCount > 1)
    {
      int drawCount = t->historyCount;
      if (t->smoothSpline && t->historyCount >= 2)
      {
        drawCount = t->historyCount < 30 ? 30 : t->historyCount;
        if (drawCount > TRAIL_HISTORY_COUNT) drawCount = TRAIL_HISTORY_COUNT;
      }

      for (int h = 0; h < drawCount; h++)
      {
        float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
        float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                      ? ComputeWidthEnvelope(t, segRatio, time)
                      : powf(segRatio, TRAIL_PROJECTILE_TAPER_POWER);
        Color nodeColor = t->gradient ? ColorGradient_Sample(t->gradient, segRatio) : c;
        if (t->alphaCurve) {
          float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
          nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
        }

        Vector3 posNode = GetInterpolatedPosition(t, segRatio);
        if (t->distortionStrength > 0.0f)
        {
          float dTime = time * t->distortionSpeed;
          float nX = (Noise_Perlin3D(posNode.x * 0.8f + dTime, posNode.y * 0.8f, posNode.z * 0.8f) - 0.5f) * 2.0f;
          float nY = (Noise_Perlin3D(posNode.x * 0.8f, posNode.y * 0.8f + dTime, posNode.z * 0.8f + 17.7f) - 0.5f) * 2.0f;
          float nZ = (Noise_Perlin3D(posNode.x * 0.8f + 31.4f, posNode.y * 0.8f, posNode.z * 0.8f + dTime) - 0.5f) * 2.0f;
          posNode.x += nX * t->distortionStrength;
          posNode.y += nY * t->distortionStrength;
          posNode.z += nZ * t->distortionStrength;
        }

        scratchOuter[h].position = posNode;
        scratchOuter[h].halfWidth = t->thickness * TRAIL_PROJECTILE_OUTER_WIDTH_MUL * taper;
        scratchOuter[h].v = segRatio * t->uvTiling - t->uvScrollOffset;
        scratchOuter[h].tint = (Color){(unsigned char)(segRatio * nodeColor.r), nodeColor.g, nodeColor.b, (unsigned char)((nodeColor.a / 255.0f) * TRAIL_PROJECTILE_OUTER_ALPHA_MAX * lifeRatio)};

        scratchInner[h].position = scratchOuter[h].position;
        scratchInner[h].halfWidth = t->thickness * TRAIL_PROJECTILE_INNER_WIDTH_MUL * taper;
        scratchInner[h].v = scratchOuter[h].v;
        scratchInner[h].tint = (Color){(unsigned char)(segRatio * nodeColor.r), nodeColor.g, nodeColor.b, (unsigned char)(nodeColor.a * lifeRatio)};
      }
      Texture2D ribbonTex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;
      DrawRibbonStrip(scratchOuter, drawCount, ribbonTex, camera);
      if (!t->disableInnerCore)
      {
        DrawRibbonStrip(scratchInner, drawCount, ribbonTex, camera);
      }
    }

    Vector3 right = camBasis->right;
    Vector3 up = camBasis->up;

    // Tối ưu hóa tính góc Rotation bằng chuẩn hóa tay
    float vx = t->velocity.x, vy = t->velocity.y, vz = t->velocity.z;
    float len2 = vx * vx + vy * vy + vz * vz;
    float rotation = 0.0f;
    if (len2 > 1e-6f)
    {
      float invL = 1.0f / sqrtf(len2);
      Vector3 vDir = {vx * invL, vy * invL, vz * invL};
      rotation = atan2f(Vector3DotProduct(vDir, up), Vector3DotProduct(vDir, right));
    }

    Color spriteTint = {128, 128, 128, (unsigned char)(255.0f * lifeRatio)};
    Rectangle uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
    if (t->spriteAnim)
      uvRect = SpriteAnim_CalculateUV(t->spriteAnim, t->maxLifetime - t->lifetime, NULL);
    float quadHeight = t->spriteAnim ? (t->length * TRAIL_PROJECTILE_QUAD_LENGTH_MUL) : (t->thickness * TRAIL_PROJECTILE_QUAD_THICK_MUL);

    if (t->sprite.id > 0)
      DrawCameraFacingQuad(camBasis, t->position, t->length * TRAIL_PROJECTILE_QUAD_LENGTH_MUL, quadHeight, rotation, spriteTint, t->sprite, uvRect);
  }
  else if (t->type == TRAIL_TYPE_WISP)
  {
    if (t->historyCount > 1)
    {
      int drawCount = t->historyCount;
      if (t->smoothSpline && t->historyCount >= 2)
      {
        drawCount = t->historyCount < 30 ? 30 : t->historyCount;
        if (drawCount > TRAIL_HISTORY_COUNT) drawCount = TRAIL_HISTORY_COUNT;
      }

      for (int h = 0; h < drawCount; h++)
      {
        float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
        float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                      ? ComputeWidthEnvelope(t, segRatio, time)
                      : ComputeWispStyleTaper(segRatio);
        Color nodeColor = c;
        if (t->gradient)
        {
          Color gradCol = ColorGradient_Sample(t->gradient, segRatio);
          nodeColor = (Color){(unsigned char)((gradCol.r / 255.0f) * c.r), (unsigned char)((gradCol.g / 255.0f) * c.g), (unsigned char)((gradCol.b / 255.0f) * c.b), (unsigned char)((gradCol.a / 255.0f) * c.a)};
        }
        if (t->alphaCurve) {
          float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
          nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
        }

        Vector3 posNode = GetInterpolatedPosition(t, segRatio);
        if (t->distortionStrength > 0.0f)
        {
          float dTime = time * t->distortionSpeed;
          float nX = (Noise_Perlin3D(posNode.x * 0.15f + dTime, posNode.y * 0.15f, posNode.z * 0.15f) - 0.5f) * 2.0f;
          float nY = (Noise_Perlin3D(posNode.x * 0.15f, posNode.y * 0.15f + dTime, posNode.z * 0.15f + 17.7f) - 0.5f) * 2.0f;
          float nZ = (Noise_Perlin3D(posNode.x * 0.15f + 31.4f, posNode.y * 0.15f, posNode.z * 0.15f + dTime) - 0.5f) * 2.0f;
          posNode.x += nX * t->distortionStrength;
          posNode.y += nY * t->distortionStrength;
          posNode.z += nZ * t->distortionStrength;
        }

        scratchOuter[h].position = posNode;
        scratchOuter[h].halfWidth = t->thickness * taper;
        scratchOuter[h].v = segRatio * t->uvTiling - t->uvScrollOffset;
        scratchOuter[h].tint = (Color){nodeColor.r, nodeColor.g, nodeColor.b, (unsigned char)((nodeColor.a / 255.0f) * 180.0f * lifeRatio * taper)};
      }
      DrawRibbonStrip(scratchOuter, drawCount, t->sprite.id > 0 ? t->sprite : s_globalTrailTex, camera);
      // WISP can optionally draw a bright hot-core layer (same as PROJECTILE/FOLLOWER)
      // Only drawn when disableInnerCore is false
      if (!t->disableInnerCore)
      {
        for (int h = 0; h < drawCount; h++)
        {
          float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
          float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                        ? ComputeWidthEnvelope(t, segRatio, time)
                        : ComputeWispStyleTaper(segRatio);
          scratchInner[h].position = scratchOuter[h].position;
          scratchInner[h].halfWidth = t->thickness * 0.35f * taper;
          scratchInner[h].v = scratchOuter[h].v;
          scratchInner[h].tint = (Color){255, 255, 255, (unsigned char)(180.0f * lifeRatio * taper)};
        }
        DrawRibbonStrip(scratchInner, drawCount, t->sprite.id > 0 ? t->sprite : s_globalTrailTex, camera);
      }
    }
  }
  else if (t->type == TRAIL_TYPE_PORTAL)
  {
    float radius = t->length;
    float age = t->maxLifetime - t->lifetime;
    if (age < TRAIL_PORTAL_SPAWN_GROW_TIME)
      radius *= (age / TRAIL_PORTAL_SPAWN_GROW_TIME);
    Rectangle uvRect = t->spriteAnim ? SpriteAnim_CalculateUV(t->spriteAnim, age, NULL) : (Rectangle){0, 0, 1, 1};
    DrawCameraFacingQuad(camBasis, t->position, radius * TRAIL_PORTAL_QUAD_SIZE_MUL, radius * TRAIL_PORTAL_QUAD_SIZE_MUL, t->angle * DEG2RAD, (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio)}, (Texture2D){0}, uvRect);
  }
  else if (t->type == TRAIL_TYPE_FOLLOWER)
  {
    if (t->historyCount > 1)
    {
      int drawCount = t->historyCount;
      if (t->smoothSpline && t->historyCount >= 2)
      {
        drawCount = t->historyCount < 30 ? 30 : t->historyCount;
        if (drawCount > TRAIL_HISTORY_COUNT) drawCount = TRAIL_HISTORY_COUNT;
      }

      for (int h = 0; h < drawCount; h++)
      {
        float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
        float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                      ? ComputeWidthEnvelope(t, segRatio, time)
                      : ComputeWispStyleTaper(segRatio);
        Color nodeColor = c;
        if (t->gradient)
        {
          Color gradCol = ColorGradient_Sample(t->gradient, segRatio);
          nodeColor = (Color){(unsigned char)((gradCol.r / 255.0f) * c.r), (unsigned char)((gradCol.g / 255.0f) * c.g), (unsigned char)((gradCol.b / 255.0f) * c.b), (unsigned char)((gradCol.a / 255.0f) * c.a)};
        }
        if (t->alphaCurve) {
          float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
          nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
        }

        Vector3 posNode = GetInterpolatedPosition(t, segRatio);
        if (t->distortionStrength > 0.0f)
        {
          float dTime = time * t->distortionSpeed;
          float nX = (Noise_Perlin3D(posNode.x * 0.8f + dTime, posNode.y * 0.8f, posNode.z * 0.8f) - 0.5f) * 2.0f;
          float nY = (Noise_Perlin3D(posNode.x * 0.8f, posNode.y * 0.8f + dTime, posNode.z * 0.8f + 17.7f) - 0.5f) * 2.0f;
          float nZ = (Noise_Perlin3D(posNode.x * 0.8f + 31.4f, posNode.y * 0.8f, posNode.z * 0.8f + dTime) - 0.5f) * 2.0f;
          posNode.x += nX * t->distortionStrength;
          posNode.y += nY * t->distortionStrength;
          posNode.z += nZ * t->distortionStrength;
        }
        scratchOuter[h].position = posNode;
        scratchOuter[h].halfWidth = t->thickness * 1.5f * taper;
        scratchOuter[h].v = segRatio * t->uvTiling - t->uvScrollOffset;
        scratchOuter[h].tint = (Color){nodeColor.r, nodeColor.g, nodeColor.b, (unsigned char)((nodeColor.a / 255.0f) * 180.0f * lifeRatio * taper)};
        scratchInner[h].position = scratchOuter[h].position;
        scratchInner[h].halfWidth = t->thickness * 0.4f * taper;
        scratchInner[h].v = scratchOuter[h].v;
        scratchInner[h].tint = (Color){255, 255, 255, (unsigned char)(255.0f * lifeRatio * taper)};
      }
      Texture2D ribbonTex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;
      DrawRibbonStrip(scratchOuter, drawCount, ribbonTex, camera);
      if (!t->disableInnerCore)
      {
        DrawRibbonStrip(scratchInner, drawCount, ribbonTex, camera);
      }
    }
  }
}

// Kiến trúc vẽ mới: Gom cụm theo BlendMode TRƯỚC, sau đó đến Shader.
// Đảm bảo không bao giờ bị ngắt Batching giữa chừng.
typedef struct
{
  BlendMode bm;
  Shader sh;
} RenderGroup;

void DrawTrailEntities(Camera3D camera)
{
  if (activeCount == 0)
    return;

  float time = (float)GetTime();
  Matrix matView = GetCameraMatrix(camera);
  TrailCameraBasis camBasis = {
      {matView.m0, matView.m4, matView.m8},
      {matView.m1, matView.m5, matView.m9},
  };

  rlDrawRenderBatchActive();
  rlDisableDepthMask();

  RenderGroup groups[32];
  int groupCount = 0;

  // Thu thập các cặp (BlendMode, Shader) duy nhất đang dùng trong Frame này
  for (int a = 0; a < activeCount; a++)
  {
    TrailEntity *t = &trailPool[s_activeIds[a]];
    Shader sh = ResolveShader(t);
    // useCustomBlendMode flag fixes BLEND_ALPHA=0 sentinel ambiguity.
    // Without it, (t->blendMode > 0) fails when blendMode=BLEND_ALPHA.
    BlendMode bm = t->useCustomBlendMode ? t->blendMode
                                         : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);

    bool found = false;
    for (int g = 0; g < groupCount; g++)
    {
      if (groups[g].bm == bm && groups[g].sh.id == sh.id)
      {
        found = true;
        break;
      }
    }
    if (!found && groupCount < 32)
    {
      groups[groupCount].bm = bm;
      groups[groupCount].sh = sh;
      groupCount++;
    }
  }

  // Vẽ lần lượt theo nhóm, giảm thiểu số lần Flush Buffer trên GPU về mức Zero
  for (int g = 0; g < groupCount; g++)
  {
    BeginBlendMode(groups[g].bm);

    Shader fullShader = groups[g].sh;
    int timeLoc = GetCachedTimeLoc(fullShader);
    if (timeLoc >= 0)
      SetShaderValue(fullShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(fullShader);

    for (int a = 0; a < activeCount; a++)
    {
      TrailEntity *t = &trailPool[s_activeIds[a]];
      BlendMode currentBm = t->useCustomBlendMode ? t->blendMode
                                                   : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);

      if (ResolveShader(t).id == fullShader.id && currentBm == groups[g].bm)
      {
        DrawTrailGeometry(t, camera, &camBasis, time);
      }
    }

    EndShaderMode();
    EndBlendMode();
  }

  rlSetTexture(0); // Dọn dẹp trạng thái Texture 1 lần duy nhất ở cuối hàm
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
}

void UnloadTrailSystem(void) {}
void TrailSystem_GetStats(int *active, int *max)
{
  *active = GetActiveTrailCount();
  *max = MAX_TRAIL_PARTICLES;
}