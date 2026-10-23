#include "core/motion/motion_body.h"
#include "core/motion/motion_fields.h"
#include "particle_surface_sampling.h"
#include "particle_field_reference.h"
#include "particle_field_capabilities.h"
#include "particle_field_integration.h"
#include "particle_system.h"
#include "core/particles/particle_manager.h"
#include "core/mesh_adjacency.h"
#include "core/emitter/particle_children.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/utils_math.h"
#include "core/ribbon_strip.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "core/gfx_quality.h"
#include "core/tuning.h"
#include "core/scene_targets.h"
#include "core/screen_distort.h"
#include "core/time_fx.h"
#include "core/wind/wind_system.h"
#include "environment/environment_system.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define MAX_PARTICLES 2000
typedef char ParticleEmissionCapacity[(MAX_PARTICLES == EMISSION_PARTICLE_PARENT_CAPACITY) ? 1 : -1];

// Trail ribbon buffer: 8 recorded history points, optionally subdivided.
#define PS_TRAIL_SUBDIV 4
#define PS_TRAIL_MAX_RIBBON_PTS 32

// Centripetal Catmull-Rom (alpha = 0.5) through p1..p2 - Messiah Engine standard.
// Eliminates loops and overshoot when particles decelerate or change trajectory abruptly.
static inline float PS_KnotInterval(Vector3 a, Vector3 b)
{
  float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
  float distSq = dx * dx + dy * dy + dz * dz;
  return (distSq > 1e-8f) ? sqrtf(sqrtf(distSq)) : 1e-4f;
}

static inline Vector3 PS_V3LerpParam(Vector3 v1, Vector3 v2, float t1, float t2, float t)
{
  float denom = t2 - t1;
  if (fabsf(denom) < 1e-5f)
    return v1;
  float factor = (t - t1) / denom;
  return (Vector3){
      v1.x + factor * (v2.x - v1.x),
      v1.y + factor * (v2.y - v1.y),
      v1.z + factor * (v2.z - v1.z)};
}

/* Sử dụng các nút knot t0..t3 đã được tính sẵn trên từng đoạn lịch sử để không
 * phải gọi lại căn bậc 4 (sqrtf(sqrtf(distSq))) cho mỗi điểm chia nhỏ (subdiv). */
static inline Vector3 PS_CatmullRomPrecomputed(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                                               float t0, float t1, float t2, float t3, float t)
{
  if (t <= 0.0f)
    return p1;
  if (t >= 1.0f)
    return p2;

  float evalT = t1 + t * (t2 - t1);

  Vector3 a1 = PS_V3LerpParam(p0, p1, t0, t1, evalT);
  Vector3 a2 = PS_V3LerpParam(p1, p2, t1, t2, evalT);
  Vector3 a3 = PS_V3LerpParam(p2, p3, t2, t3, evalT);

  Vector3 b1 = PS_V3LerpParam(a1, a2, t0, t2, evalT);
  Vector3 b2 = PS_V3LerpParam(a2, a3, t1, t3, evalT);

  return PS_V3LerpParam(b1, b2, t1, t2, evalT);
}

static inline Vector3 PS_CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
  if (t <= 0.0f)
    return p1;
  if (t >= 1.0f)
    return p2;

  float t0 = 0.0f;
  float t1 = t0 + PS_KnotInterval(p0, p1);
  float t2 = t1 + PS_KnotInterval(p1, p2);
  float t3 = t2 + PS_KnotInterval(p2, p3);

  return PS_CatmullRomPrecomputed(p0, p1, p2, p3, t0, t1, t2, t3, t);
}

static bool PS_AxisCrossBasis(Vector3 authoredAxis, Vector3 viewDir,
                              Vector3 cameraUp, Vector3 *sideA,
                              Vector3 *sideB)
{
  const float facingDirectionLengthSq = Vector3LengthSqr(authoredAxis);
  if (facingDirectionLengthSq < 1e-8f)
    return false;

  Vector3 axis = Vector3Scale(authoredAxis, 1.0f / sqrtf(facingDirectionLengthSq));
  Vector3 first = Vector3CrossProduct(axis, viewDir);
  if (Vector3LengthSqr(first) < 1e-8f)
    first = Vector3CrossProduct(axis, cameraUp);
  if (Vector3LengthSqr(first) < 1e-8f)
    first = Vector3CrossProduct(axis, (Vector3){1.0f, 0.0f, 0.0f});
  if (Vector3LengthSqr(first) < 1e-8f)
    first = Vector3CrossProduct(axis, (Vector3){0.0f, 0.0f, 1.0f});

  first = Vector3Normalize(first);
  *sideA = first;
  *sideB = Vector3Normalize(Vector3CrossProduct(axis, first));
  return true;
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
  Vector3 forceAxisOrigin;
  Vector3 forceAxisDir;
  const ParticleTravelPath *travelPath;
  Vector3 travelFormationOffset;
  int travelWaypoint;
  bool travelImpactActive;
  float travelImpactAge;
  float windInfluence;
  float drag;
  const ParticleDynamicsProfile *dynamics;
  ParticleDynamicsProfile dynamicsStorage;
  bool receiveMotionFields;
  bool spatialMotionOnly;
  MotionReceiver motionReceiver;
  Vector3 dynamicsInitialImpulseNs;
  Vector3 dynamicsInitialAccelerationMps2;
  Vector3 dynamicsConstantForceNewtons;
  bool dynamicsImpulsePending;
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
  unsigned int texId;        // 0 = use the batch default passed to DrawParticles
  int blendMode;             // VFX_BlendMode — see the blend law in vfx_config.h
  int unlit;                 // 1 = emissive, skip the lighting multiply
  float emissiveBoost;       // >1 = HDR headroom for a glowing core (see vfx_config.h)
  int volumeSheet;           // 1 = texture is a packed 4-channel volume flipbook
  unsigned int rampTexId;    // black-body/magic LUT for volumeSheet; 0 = none
  float heatGain;            // exposure on the sheet's emission before the ramp
  float smokeGain;           // multiplier on the sheet's soot density
  Color smokeTint;           // colour of the soot half (black/white smoke)
  int sixWayLighting;        // 0 = off, 1 = synthetic 6-way, 2 = dual-texture 6-way pair
  unsigned int sixWayTexBId; // Map B texture id for dual-texture 6-way
  float sixWayScattering;    // forward scatter / backlit boost factor
  float sixWayAbsorption;    // multi-axis extinction factor
  int smokeSheet;            // 1 = extracted EOO smoke body + BC5 normal companion
  unsigned int normalTexId;
  unsigned int motionTexId; // 2D optical flow texture id for subframe advection
  float motionWarpScale;    // motion vector displacement scale
  VFXContrastProfileId contrastProfile;
  const SkillCurve *radiusCurve;
  const SkillCurve *speedCurve;
  const SkillCurve *alphaCurve;
  const SkillCurve *emissiveCurve;

  EmissionChildrenHandle children;

  // Stretch rendering
  float stretchStrength;
  float stretchMinSpeed;

  // Physical collision
  bool collisionEnabled;
  float collisionElasticity;
  float collisionFloorY;
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

  // Advanced facing & 3D Mesh (Niagara parity)
  VFX_FacingMode facingMode;
  Vector3 facingDirection;
  float facingAspect;
  Model meshModel;
  Vector3 meshRotation;     // Euler angles (pitch, yaw, roll in radians)
  Vector3 meshRotationRate; // Angular rotation speed in rad/s
} ParticleInternal;

static ParticleInternal g_Particles[MAX_PARTICLES];
static float s_particleTime = 0.0f;

static int s_freeHead = 0;
static int s_nextFree[MAX_PARTICLES];
static int s_activeIds[MAX_PARTICLES];
static int s_activeCount = 0;
static int s_slotListIndex[MAX_PARTICLES];

// Cờ theo dõi trạng thái dữ liệu giữa các pass render trong cùng 1 frame
static bool s_depthSortDirty = true;
static Vector3 s_lastSortCamPos = {0};
static Vector3 s_lastSortCamTarget = {0};

static inline void Particle_Deactivate(int idx)
{
  if (idx < 0 || idx >= MAX_PARTICLES || !g_Particles[idx].active)
    return;
  int listIdx = s_slotListIndex[idx];
  if (listIdx < 0 || listIdx >= s_activeCount)
    return;

  EmissionChildren_Release(g_Particles[idx].children);
  g_Particles[idx].children = (EmissionChildrenHandle){0};
  g_Particles[idx].active = false;

  int lastId = s_activeIds[s_activeCount - 1];
  s_activeIds[listIdx] = lastId;
  s_slotListIndex[lastId] = listIdx;
  s_activeCount--;
  s_slotListIndex[idx] = -1;

  s_nextFree[idx] = s_freeHead;
  s_freeHead = idx;
  s_depthSortDirty = true;
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
  s_depthSortDirty = true;
  return idx;
}

// Đợt E / F1 — lit particles.
static float s_lightingStrength = 0.0f;
static float s_scatterStrength = 0.0f; // shape-neutral: raise only for backlit glow
static float s_debugNormal = 0.0f;     // 1 = paint particle normals as RGB
static float s_normalBulge = 1.0f;     // 1 = true hemisphere; >1 exaggerates
static float s_sunGain = 1.0f;         // night sun/ambient are ~0.2 — without
static float s_ambientGain = 0.25f;    // gain, lighting just dims the smoke
static float s_lightAzimuth = -1.0f;   // <0 = real sun; >=0 = debug override
static float s_analyticUV = 1.0f;      // 0 only if a SpriteAnim atlas is in use

static float s_softFade = 0.35f;
static float s_softDebug = 0.0f;
static int s_locSoftFade = -1;
static int s_locSoftDebug = -1;
static bool s_softBound = false;
#define PARTICLE_SOFT_DEPTH_SLOT 3
static int s_locAtlasGrid = -1;
static int s_locEmissiveBoost = -1;
static float s_emissiveBoost = 1.0f; // GLOBAL multiplier; per-particle value carries the intent

static void SpawnEmissionChild(void *user, ParticleConfig child)
{
  (void)user;
  SpawnParticle(child);
}
static float EmissionRandom01(void *user)
{
  (void)user;
  return Random01();
}

void InitParticleSystem(void)
{
  EmissionChildren_Init(SpawnEmissionChild, EmissionRandom01, NULL);
  for (int i = 0; i < MAX_PARTICLES - 1; i++)
    s_nextFree[i] = i + 1;
  s_nextFree[MAX_PARTICLES - 1] = MAX_PARTICLES;
  s_freeHead = 0;
  s_activeCount = 0;
  s_depthSortDirty = true;
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
  if (!ParticleField_CpuSupported(config.forceField) ||
      (config.travelPath && !ParticleField_CpuSupported(config.travelPath->arrivalForceField)))
  {
    TraceLog(LOG_WARNING, "ParticleSystem: CPU particle rejected unsupported vector-texture field");
    return;
  }
  if (config.render.texture.id == 0)
  {
    config.render.texture = ParticleSystem_DefaultSprite();
    if (config.render.gradient == NULL)
    {
      config.colorStart.r = config.colorStart.g = config.colorStart.b = 255;
      config.colorEnd.r = config.colorEnd.g = config.colorEnd.b = 255;
    }
  }
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
  p->maxLifetime = config.lifetime > 1e-6f ? config.lifetime : 1e-6f;
  p->emitterId = emitterId;
  p->renderMode = renderMode;

  p->forceField = config.forceField;
  p->forceAxisOrigin = config.forceAxisOrigin;
  p->forceAxisDir = config.forceAxisDir;
  p->travelPath = config.travelPath;
  p->travelFormationOffset = (Vector3){0};
  if (config.travelPath && config.travelPath->formationOrigin)
    p->travelFormationOffset = Vector3Subtract(config.position,
                                               *config.travelPath->formationOrigin);
  p->travelWaypoint = 0;
  p->travelImpactActive = false;
  p->travelImpactAge = 0.0f;
  p->windInfluence = config.physics.windInfluence > 0.0f ? config.physics.windInfluence : config.windInfluence;
  p->drag = config.physics.drag > 0.0f ? config.physics.drag : config.drag;
  p->dynamics = NULL;
  if (config.physics.dynamics)
  {
    p->dynamicsStorage = *config.physics.dynamics;
    if (p->drag > 0.0f && p->dynamicsStorage.linearDragPerSecond <= 0.0f)
    {
      p->dynamicsStorage.linearDragPerSecond = p->drag;
    }
    p->dynamics = &p->dynamicsStorage;
  }
  p->receiveMotionFields = config.physics.receiveMotionFields;
  p->spatialMotionOnly = config.physics.spatialMotionOnly;
  p->motionReceiver = (MotionReceiver){0};
  if (p->receiveMotionFields && !p->spatialMotionOnly && config.physics.initialGuide)
    MotionFields_Capture(config.physics.initialGuide, config.position, &p->motionReceiver);
  p->dynamicsInitialImpulseNs = config.physics.initialImpulseNs;
  p->dynamicsInitialAccelerationMps2 = config.physics.initialAccelerationMps2;
  p->dynamicsConstantForceNewtons = config.physics.constantForceNewtons;
  p->dynamicsImpulsePending = p->dynamics != NULL;
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
                                   ? config.render.emissiveBoost
                                   : 1.0f,
          .emissionThreshold = 1.0f,
          .unlit = config.render.unlit != 0});
  if (particleAppearance.surface == VFX_SURFACE_PREMULTIPLIED &&
      !config.render.volumeSheet)
    particleAppearance.surface = VFX_SURFACE_ALPHA;
  p->blendMode = (int)particleAppearance.surface;
  p->unlit = particleAppearance.unlit ? 1 : 0;
  p->contrastProfile = particleAppearance.contrast;
  p->emissiveBoost = particleAppearance.emissionIntensity > 0.0f
                         ? particleAppearance.emissionIntensity
                         : 1.0f;
  p->volumeSheet = config.render.volumeSheet;
  p->rampTexId = config.render.rampLUT.id;
  p->heatGain = (config.render.heatGain > 0.0f) ? config.render.heatGain : 1.0f;
  p->smokeGain = (config.render.smokeGain > 0.0f) ? config.render.smokeGain : 1.0f;
  p->smokeTint = (config.render.smokeTint.a != 0) ? config.render.smokeTint
                                                  : (Color){82, 74, 69, 255};
  p->sixWayLighting = p->unlit ? 0 : config.render.sixWayLighting;
  p->sixWayTexBId = config.render.sixWayTexB.id;
  p->sixWayScattering = (config.render.sixWayScattering > 0.0f) ? config.render.sixWayScattering : 1.0f;
  p->sixWayAbsorption = (config.render.sixWayAbsorption > 0.0f) ? config.render.sixWayAbsorption : 1.0f;
  p->smokeSheet = config.render.smokeSheet;
  p->normalTexId = config.render.normalTex.id;
  p->motionTexId = config.render.motionTex.id;
  p->motionWarpScale = config.render.motionWarpScale;
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

  p->children = EmissionChildren_Bind(targetIdx, &config);

  // Populate stretch, collision, and trail parameters
  p->stretchStrength = config.render.stretchStrength;
  p->stretchMinSpeed = config.render.stretchMinSpeed;

  p->collisionEnabled = config.physics.collisionEnabled;
  p->collisionElasticity = config.physics.collisionElasticity;
  p->collisionFloorY = config.physics.collisionFloorY;
  p->trailLength = config.render.trailLength;
  if (p->trailLength > 8)
    p->trailLength = 8; // clamp to static buffer size
  p->trailWidthRatio = config.render.trailWidthRatio;
  p->trailColorStart = config.render.trailColorStart;
  p->trailColorEnd = config.render.trailColorEnd;
  p->trailStepTime = (config.render.trailStepTime > 0.0f) ? config.render.trailStepTime : 0.015f;
  p->trailOnly = (p->trailLength > 0) ? config.render.trailOnly : 0;
  p->trailSmooth = config.render.trailSmooth;
  p->trailHistoryCount = 0;
  p->trailHistoryTimer = 0.0f;

  // Advanced facing & 3D Mesh (Niagara parity)
  p->facingMode = config.render.facingMode;
  p->facingDirection = config.render.facingDirection;
  p->facingAspect = config.render.facingAspect > 0.0f ? config.render.facingAspect : 1.0f;
  p->meshModel = config.render.meshModel;
  p->meshRotation = (Vector3){0.0f, 0.0f, 0.0f};
  p->meshRotationRate = config.render.meshRotationRate;
}

static inline bool PS_PointInAABB(Vector3 p, Vector3 lo, Vector3 hi, float pad)
{
  return p.x >= lo.x - pad && p.x <= hi.x + pad &&
         p.y >= lo.y - pad && p.y <= hi.y + pad &&
         p.z >= lo.z - pad && p.z <= hi.z + pad;
}

void UpdateParticles(float dt)
{
  if (dt <= 0.0f || !isfinite(dt))
    return;
  s_particleTime += dt;
  if (s_activeCount == 0)
    return;

  s_depthSortDirty = true;
  const bool windActive = WindZone_IsActive();
  const int activeWindVorticles = Wind_GetActiveCount();

  // Tiền tính toán Broadphase AABB cho toàn bộ Motion Fields có tác động lên Particle
  Vector3 motionLo = {0}, motionHi = {0};
  const bool hasAnyMotionFields =
      (MotionFields_GetGuideCount() + MotionFields_GetTargetCount()) > 0 &&
      MotionFields_GetAnchoredBounds(MOTION_RECEIVER_PARTICLE, &motionLo, &motionHi);

  // Nếu không có vorticle cục bộ, gió nền là đồng nhất trong không gian -> lấy mẫu 1 lần cho cả frame
  const MediumProperties uniformBaseMedium =
      (activeWindVorticles == 0)
          ? Wind_EvaluateBackgroundMedium((Vector3){0, 0, 0}, s_particleTime, (Vector3){0, 0, 0})
          : (MediumProperties){0};

  // TỐI ƯU 2: Lặp ngược (Reverse Loop).
  for (int a = s_activeCount - 1; a >= 0; a--)
  {
    int i = s_activeIds[a];
    ParticleInternal *p = &g_Particles[i];

    p->lifetime -= dt;
    p->rotation += p->angularVelocity * dt;
    p->meshRotation.x += p->meshRotationRate.x * dt;
    p->meshRotation.y += p->meshRotationRate.y * dt;
    p->meshRotation.z += p->meshRotationRate.z * dt;

    if (p->lifetime <= 0.0f)
    {
      EmissionChildrenHandle deadChildren = p->children;
      p->children = (EmissionChildrenHandle){0};
      Vector3 deathPos = {p->x, p->y, p->z};
      Vector3 deathVel = {p->vx, p->vy, p->vz};
      Particle_Deactivate(i);
      EmissionChildren_Publish(deadChildren, &(EmissionEvent){.kind = EMISSION_EVENT_DEATH,
                                                              .position = deathPos,
                                                              .velocity = deathVel});
      EmissionChildren_Release(deadChildren);
      continue;
    }

    float invMaxLife = 1.0f / p->maxLifetime;
    float ageT = 1.0f - Clamp(p->lifetime * invMaxLife, 0.0f, 1.0f);

    if (p->followTarget != NULL &&
        (p->followTargetGeneration == NULL ||
         *p->followTargetGeneration == p->followGeneration))
    {
      Vector3 now = *p->followTarget;
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

    if (p->children != 0)
    {
      float speedMul = p->speedCurve ? SkillCurve_Eval(p->speedCurve, ageT) : 1.0f;
      EmissionChildren_Publish(p->children, &(EmissionEvent){.kind = EMISSION_EVENT_LIVE,
                                                             .position = {p->x, p->y, p->z},
                                                             .velocity = {p->vx, p->vy, p->vz},
                                                             .dt = dt,
                                                             .stepDisplacement = {p->vx * dt * speedMul, p->vy * dt * speedMul, p->vz * dt * speedMul}});
    }

    const ForceField *activeField = p->forceField;
    if (p->travelImpactActive)
    {
      p->travelImpactAge += dt;
      activeField = (p->travelPath && p->travelPath->arrivalForceField &&
                     (p->travelPath->arrivalForceDuration <= 0.0f || p->travelImpactAge <= p->travelPath->arrivalForceDuration))
                        ? p->travelPath->arrivalForceField
                        : NULL;
    }

    if (p->receiveMotionFields || p->spatialMotionOnly)
    {
      ParticleDynamicsProfile fallback = {.inverseMassKg = 1.0f, .gravityScale = 0.0f, .windCouplingHz = 3.5f, .windSusceptibility = p->windInfluence};
      const ParticleDynamicsProfile *body = p->dynamics ? p->dynamics : &fallback;
      Vector3 position = {p->x, p->y, p->z}, velocity = {p->vx, p->vy, p->vz};
      if (p->dynamicsImpulsePending)
      {
        velocity = ParticleDynamics_ApplyImpulse(velocity, p->dynamicsInitialImpulseNs,
                                                 body->inverseMassKg > 0.0f ? body->inverseMassKg : 1.0f);
        p->dynamicsImpulsePending = false;
      }

      float remaining = fminf(dt, 0.066f);
      bool destroyed = false;
      BodyPhysicalProperties physicalBody = MotionBody_GetPhysicalProperties(body);
      ReceiverConstraints constraints = {.mode = RECEIVER_FREE, .permittedAxes = {1.0f, 1.0f, 1.0f}};
      Vector3 gravityVec = {0.0f, -9.81f * body->gravityScale, 0.0f};
      MediumProperties baseMedium;
      if (activeWindVorticles > 0)
      {
        baseMedium = Wind_EvaluateBackgroundMedium(position, s_particleTime, gravityVec);
      }
      else
      {
        baseMedium = uniformBaseMedium;
        baseMedium.gravityMps2 = gravityVec;
      }
      if (body->airDensityKgM3 > 0.0f)
        baseMedium.densityKgM3 = body->airDensityKgM3;

      // Kiểm tra nhanh xem hạt có nằm gần bất kỳ MotionField nào hoặc đang bị Guide bắt giữ không
      float speedEst = fabsf(velocity.x) + fabsf(velocity.y) + fabsf(velocity.z);
      float sweepPad = speedEst * remaining + 1.0f;
      bool canHitMotionFields = (p->motionReceiver.guide != 0) ||
                                (hasAnyMotionFields && PS_PointInAABB(position, motionLo, motionHi, sweepPad));

      // Chỉ chia 2 substeps đều nhau khi frametime > 22.2ms (< 45 FPS) VÀ hạt nằm trong vùng MotionField.
      // Tránh lỗi ở 60 FPS (dt ~ 0.0168s) bị tách thành 0.0166s + 0.0002s làm nhân đôi chi phí lấy mẫu!
      int numSteps = (canHitMotionFields && remaining > (1.0f / 45.0f)) ? 2 : 1;
      float fixedStep = remaining / (float)numSteps;

      for (int substepIdx = 0; substepIdx < numSteps; ++substepIdx)
      {
        float step = fixedStep;
        float timeOffset = -remaining;
        float sampleTime = s_particleTime + timeOffset;
        remaining -= step;

        MediumProperties medium = baseMedium;
        if (substepIdx > 0 && activeWindVorticles > 0)
        {
          medium = Wind_EvaluateBackgroundMedium(position, sampleTime, gravityVec);
          if (body->airDensityKgM3 > 0.0f)
            medium.densityKgM3 = body->airDensityKgM3;
        }

        FieldSample sample = {0};
        if (canHitMotionFields)
        {
          if (p->spatialMotionOnly)
            MotionFields_SampleSpatialBodyAtOffset(position, velocity, &physicalBody, &medium, &constraints,
                                                   step, timeOffset, MOTION_RECEIVER_PARTICLE, p->receiveMotionFields ? &p->motionReceiver : NULL, &sample);
          else
            MotionFields_SampleBodyAtOffset(position, velocity, &physicalBody, &medium, &constraints,
                                            step, timeOffset, MOTION_RECEIVER_PARTICLE, &p->motionReceiver, &sample);
        }

        Vector3 acceleration = p->dynamicsInitialAccelerationMps2;
        if (activeField)
          acceleration = MotionVec_Add(acceleration, ForceField_Evaluate(
                                                         activeField, position, velocity, sampleTime, p->forceAxisOrigin, p->forceAxisDir));
        if (windActive)
          acceleration = MotionVec_Add(acceleration, MotionVec_Scale(
                                                         WindZone_Evaluate(position, velocity, sampleTime), body->windAccelerationScale * body->windSusceptibility));

        velocity = MotionBody_AdvanceFieldVelocity(velocity, body, acceleration,
                                                   p->dynamicsConstantForceNewtons, &sample, medium.velocityMps, step);
        if (activeField)
          velocity = MotionVec_Scale(velocity, ForceField_GetViscosityDamping(activeField, step));

        Vector3 previous = position;
        position = MotionVec_Add(position, MotionVec_Scale(velocity, step));

        if (p->motionReceiver.guide != 0)
        {
          bool wasArrived = p->motionReceiver.arrived;
          MotionArrivalMode action = MotionFields_AdvanceReceiver(&p->motionReceiver, previous, position, velocity);
          if (!wasArrived && p->motionReceiver.arrived)
          {
            MotionArrivalProfile arrival;
            if (MotionFields_GetArrival(p->motionReceiver.guide, &arrival))
            {
              velocity = ParticleDynamics_ApplyImpulse(velocity, arrival.impulseNs, body->inverseMassKg > 0.0f ? body->inverseMassKg : 1.0f);
              if (arrival.overrideDynamics)
              {
                p->dynamicsStorage = arrival.dynamics;
                p->dynamics = &p->dynamicsStorage;
                body = p->dynamics;
                physicalBody = MotionBody_GetPhysicalProperties(body);
              }
            }
            EmissionChildren_Publish(p->children, &(EmissionEvent){.kind = EMISSION_EVENT_ARRIVAL,
                                                                   .position = position,
                                                                   .velocity = velocity});
            if (action == MOTION_ARRIVAL_DESTROY)
            {
              destroyed = true;
              break;
            }
          }
        }
      }
      p->x = position.x;
      p->y = position.y;
      p->z = position.z;
      p->vx = velocity.x;
      p->vy = velocity.y;
      p->vz = velocity.z;
      if (destroyed)
      {
        Particle_Deactivate(i);
        continue;
      }
      goto particle_contacts;
    }

    if (p->dynamics)
    {
      const ParticleDynamicsProfile *d = p->dynamics;
      Vector3 position = {p->x, p->y, p->z};
      Vector3 velocity = {p->vx, p->vy, p->vz};
      BodyPhysicalProperties physicalBody = MotionBody_GetPhysicalProperties(d);
      Vector3 gravityVec = {0.0f, -9.81f * d->gravityScale, 0.0f};
      MediumProperties medium;
      if (activeWindVorticles > 0)
      {
        medium = Wind_EvaluateBackgroundMedium(position, s_particleTime, gravityVec);
      }
      else
      {
        medium = uniformBaseMedium;
        medium.gravityMps2 = gravityVec;
      }
      if (d->airDensityKgM3 > 0.0f)
        medium.densityKgM3 = d->airDensityKgM3;

      FieldSample externalSample = {0};
      // Broadphase culling: chỉ lấy mẫu MotionFields khi điểm nằm trong AABB của các trường đang hoạt động
      if (hasAnyMotionFields && PS_PointInAABB(position, motionLo, motionHi, 0.25f))
      {
        ReceiverConstraints constraints = {.mode = RECEIVER_FREE, .permittedAxes = {1.0f, 1.0f, 1.0f}};
        MotionFields_SampleExternalBodyStep(position, velocity, &physicalBody, &medium,
                                            &constraints, dt, MOTION_RECEIVER_PARTICLE, &externalSample);
      }

      ParticleDynamicsProfile integrationBody = *d;
      if (externalSample.hasBuoyancyForce)
        integrationBody.densityKgM3 = 0.0f;
      if (externalSample.hasDragForce)
        integrationBody.windCouplingHz = 0.0f;
      d = &integrationBody;

      Vector3 acceleration = MotionVec_Add(p->dynamicsInitialAccelerationMps2,
                                           externalSample.accelerationMps2);
      float inverseMass = d->inverseMassKg > 0.0f ? d->inverseMassKg : 1.0f;
      if (p->dynamicsImpulsePending)
      {
        velocity = ParticleDynamics_ApplyImpulse(velocity,
                                                 p->dynamicsInitialImpulseNs,
                                                 inverseMass);
        p->dynamicsImpulsePending = false;
      }
      acceleration.y += ParticleDynamics_GravityAcceleration(d);
      if (activeField)
      {
        Vector3 fieldAcceleration = ForceField_Evaluate(
            activeField, position, velocity, p->lifetime,
            p->forceAxisOrigin, p->forceAxisDir);
        acceleration.x += fieldAcceleration.x;
        acceleration.y += fieldAcceleration.y;
        acceleration.z += fieldAcceleration.z;
      }
      if (windActive)
      {
        Vector3 windAcceleration = WindZone_Evaluate(position, velocity,
                                                     s_particleTime);
        float windScale = d->windAccelerationScale * d->windSusceptibility;
        acceleration.x += windAcceleration.x * windScale;
        acceleration.y += windAcceleration.y * windScale;
        acceleration.z += windAcceleration.z * windScale;
      }
      if (ParticleField_UsesSharedIntegration(&externalSample))
      {
        Vector3 otherAcceleration = MotionVec_Sub(acceleration, externalSample.accelerationMps2);
        otherAcceleration.y -= ParticleDynamics_GravityAcceleration(d);
        velocity = MotionBody_AdvanceFieldVelocity(velocity, d, otherAcceleration,
                                                   p->dynamicsConstantForceNewtons, &externalSample, medium.velocityMps, dt);
        if (activeField)
          velocity = MotionVec_Scale(velocity,
                                     ForceField_GetViscosityDamping(activeField, dt));
      }
      else
      {
        velocity = ParticleDynamics_ApplyAccelerationAndForce(
            velocity, acceleration, MotionVec_Add(p->dynamicsConstantForceNewtons, externalSample.forceNewtons), inverseMass, dt);
        if (activeField)
        {
          float viscosity = ForceField_GetViscosityDamping(activeField, dt);
          velocity.x *= viscosity;
          velocity.y *= viscosity;
          velocity.z *= viscosity;
        }
        velocity = ParticleDynamics_ApplyLinearDrag(velocity,
                                                    d->linearDragPerSecond, dt);
        velocity = ParticleDynamics_ClampTerminalSpeed(velocity, d->terminalSpeedMps);
        if ((d->windCouplingHz > 0.0f && d->windSusceptibility > 0.0f) ||
            p->travelImpactActive)
        {
          float couplingHz = d->windCouplingHz;
          float susceptibility = d->windSusceptibility;
          Vector3 airflow = externalSample.mediumIsAbsolute ? externalSample.mediumVelocityMps : MotionVec_Add(medium.velocityMps, externalSample.mediumVelocityMps);
          velocity = ParticleDynamics_CoupleToAirflow(velocity, airflow,
                                                      couplingHz * susceptibility, dt);
        }
      }
      if (p->travelPath && !p->travelImpactActive &&
          d->steeringFrequencyHz > 0.0f)
      {
        float maxSteering = d->maxSteeringAccelMps2 > 0.0f
                                ? d->maxSteeringAccelMps2
                                : p->travelPath->maxAcceleration;
        Vector3 steering = ParticleTravel_ComputeCriticalDampedAcceleration(
            p->travelPath, position, velocity, &p->travelWaypoint,
            p->travelFormationOffset, d->steeringFrequencyHz, maxSteering);
        velocity.x += steering.x * dt;
        velocity.y += steering.y * dt;
        velocity.z += steering.z * dt;
      }
      p->vx = velocity.x;
      p->vy = velocity.y;
      p->vz = velocity.z;
      p->x += velocity.x * dt;
      p->y += velocity.y * dt;
      p->z += velocity.z * dt;
      if (p->travelPath && !p->travelImpactActive &&
          ParticleTravel_AdvancePhysical(p->travelPath, position,
                                         (Vector3){p->x, p->y, p->z}, &p->travelWaypoint,
                                         p->travelFormationOffset))
      {
        Vector3 blastPos = p->travelPath->target ? *p->travelPath->target : (Vector3){p->x, p->y, p->z};
        Vector3 impactOffset = ParticleTravel_TransportOffset(p->travelPath, p->travelWaypoint, p->travelFormationOffset);
        p->x = blastPos.x + impactOffset.x;
        p->y = blastPos.y + impactOffset.y;
        p->z = blastPos.z + impactOffset.z;
        EmissionChildren_Publish(p->children, &(EmissionEvent){.kind = EMISSION_EVENT_ARRIVAL,
                                                               .position = {p->x, p->y, p->z},
                                                               .velocity = {p->vx, p->vy, p->vz}});
        Vector3 enteredPosition = {p->x, p->y, p->z}, enteredVelocity = {p->vx, p->vy, p->vz};
        ParticleTravel_ApplyImpactEntry(p->travelPath, &enteredPosition, &enteredVelocity);
        p->x = enteredPosition.x;
        p->y = enteredPosition.y;
        p->z = enteredPosition.z;
        p->vx = enteredVelocity.x;
        p->vy = enteredVelocity.y;
        p->vz = enteredVelocity.z;
        p->travelImpactActive = true;
        p->travelImpactAge = 0.0f;
        continue;
      }
      goto particle_contacts;
    }

    // TỐI ƯU 3: Inline Math Scalar (Tính toán trực tiếp trên x, y, z)
    if (activeField)
    {
      Vector3 pos = {p->x, p->y, p->z};
      Vector3 vel = {p->vx, p->vy, p->vz};
      Vector3 force = ForceField_Evaluate(activeField, pos, vel, p->lifetime,
                                          p->forceAxisOrigin, p->forceAxisDir);
      p->vx += force.x * dt;
      p->vy += force.y * dt;
      p->vz += force.z * dt;

      float viscDamp = ForceField_GetViscosityDamping(activeField, dt);
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

    if (p->drag > 0.0f)
    {
      float dragFactor = expf(-p->drag * dt);
      p->vx *= dragFactor;
      p->vy *= dragFactor;
      p->vz *= dragFactor;
    }

    float speedMul = p->speedCurve ? SkillCurve_Eval(p->speedCurve, ageT) : 1.0f;
    float step = dt * speedMul;
    bool reachedTarget = false;
    if (p->travelPath && !p->travelImpactActive)
    {
      Vector3 position = {p->x, p->y, p->z};
      Vector3 velocity = {p->vx, p->vy, p->vz};
      reachedTarget = ParticleTravel_StepFormation(p->travelPath, step, &position,
                                                   &velocity, &p->travelWaypoint,
                                                   p->travelFormationOffset);
      p->x = position.x;
      p->y = position.y;
      p->z = position.z;
      p->vx = velocity.x;
      p->vy = velocity.y;
      p->vz = velocity.z;
    }
    else
    {
      p->x += p->vx * step;
      p->y += p->vy * step;
      p->z += p->vz * step;
      if (p->windInfluence > 0.0f)
      {
        Vector3 windVel = (activeWindVorticles > 0)
                              ? Wind_EvaluateVelocity((Vector3){p->x, p->y, p->z}, s_particleTime)
                              : uniformBaseMedium.velocityMps;
        Vector3 vel = (Vector3){p->vx, p->vy, p->vz};
        vel = MotionProfile_CoupleToAirflow(vel, windVel, 3.5f * p->windInfluence, dt);
        p->vx = vel.x;
        p->vy = vel.y;
        p->vz = vel.z;
      }
    }

    if (reachedTarget)
    {
      if (p->travelPath->arrivalForceField)
      {
        Vector3 position = {p->x, p->y, p->z};
        Vector3 velocity = {p->vx, p->vy, p->vz};
        ParticleTravel_ApplyImpactEntry(p->travelPath, &position, &velocity);
        p->x = position.x;
        p->y = position.y;
        p->z = position.z;
        p->vx = velocity.x;
        p->vy = velocity.y;
        p->vz = velocity.z;
        p->travelImpactActive = true;
        p->travelImpactAge = 0.0f;
        continue;
      }
      EmissionChildrenHandle arrChildren = p->children;
      p->children = (EmissionChildrenHandle){0};
      Vector3 arrPos = {p->x, p->y, p->z};
      Vector3 arrVel = {p->vx, p->vy, p->vz};
      Particle_Deactivate(i);
      EmissionChildren_Publish(arrChildren, &(EmissionEvent){.kind = EMISSION_EVENT_ARRIVAL,
                                                             .position = arrPos,
                                                             .velocity = arrVel});
      EmissionChildren_Release(arrChildren);
      continue;
    }

  particle_contacts:
    if (activeField)
    {
      Vector3 position = {p->x, p->y, p->z};
      Vector3 velocity = {p->vx, p->vy, p->vz};
      ForceField_ResolveParticleContacts(activeField, p->radius, &position, &velocity);
      p->x = position.x;
      p->y = position.y;
      p->z = position.z;
      p->vx = velocity.x;
      p->vy = velocity.y;
      p->vz = velocity.z;
    }
    // Ground / Terrain Collision
    if (p->collisionEnabled)
    {
      float floorY = p->collisionFloorY;
      float terrainY = 0.0f;
      if (Wind_SampleTerrain(p->x, p->z, &terrainY))
      {
        if (terrainY > floorY)
          floorY = terrainY;
      }
      if (p->y <= floorY)
      {
        p->vy = -p->vy * p->collisionElasticity;
        p->vx *= 0.75f;
        p->vz *= 0.75f;
        p->y = floorY + 0.005f;

        EmissionChildren_Publish(p->children, &(EmissionEvent){.kind = EMISSION_EVENT_COLLISION,
                                                               .position = {p->x, floorY + 0.005f, p->z},
                                                               .velocity = {p->vx, p->vy, p->vz},
                                                               .normal = {0.0f, 1.0f, 0.0f}});
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
        if (maxLen > 8)
          maxLen = 8;
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
      else
      {
        // Giữ điểm đầu tiên của trail luôn bám sát vị trí hạt hiện tại mỗi frame
        p->trailHistory[0] = (Vector3){p->x, p->y, p->z};
      }
    }
  }
}

static float s_particleDepths[MAX_PARTICLES];

// Chuỗi gap Ciura tối ưu cho Shell Sort (nhanh hơn 30-40% so với gap = count / 2)
static void SortParticlesByDepth(int *ids, int count, const float *depths)
{
  static const int ciuraGaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
  for (int g = 0; g < 8; g++)
  {
    int gap = ciuraGaps[g];
    if (gap >= count)
      continue;
    for (int i = gap; i < count; i++)
    {
      int temp = ids[i];
      float tempDepth = depths[temp];
      int j = i;
      while (j >= gap && depths[ids[j - gap]] > tempDepth)
      {
        ids[j] = ids[j - gap];
        j -= gap;
      }
      ids[j] = temp;
    }
  }
}

// ─── Đợt E / F1 — lit particles ──────────────────────────────────────────────
static Shader s_litShader = {0};
static int s_locBgLuma = -1;
static int s_locBgAdapt = -1;
static float s_bgAdapt = 0.0f;
static bool s_litShaderTried = false;
static bool s_litActive = false;
static int s_locSunToLight, s_locSunColor, s_locAmbient, s_locViewPos;
static int s_locLightStrength, s_locScatterStrength;
static int s_locVfxCount, s_locVfxPos, s_locVfxColor, s_locVfxRadius;
static int s_locDebugNormal, s_locNormalBulge, s_locSunGain, s_locAmbientGain;
static int s_locLightAzimuth, s_locAnalyticUV;
static int s_locVolumeSheet = -1, s_locRampLUT = -1, s_locHeatGain = -1,
           s_locSmokeTint = -1, s_locSmokeGain = -1;
static int s_locSixWayLighting = -1, s_locSixWayTexB = -1,
           s_locSixWayScattering = -1, s_locSixWayAbsorption = -1,
           s_locAmbientGround = -1, s_locAmbientHorizon = -1;
static int s_locNormalTex = -1;
static int s_locUseMotionVectors = -1, s_locMotionTex = -1, s_locMotionWarp = -1;
static int s_locTime = -1;

#define PARTICLE_MAX_VFX_LIGHTS 4

void ParticleSystem_SetLighting(float strength01, float scatter01)
{
  s_lightingStrength = strength01 < 0.0f ? 0.0f : (strength01 > 1.0f ? 1.0f : strength01);
  s_scatterStrength = scatter01 < 0.0f ? 0.0f : (scatter01 > 2.0f ? 2.0f : scatter01);
}

void ParticleSystem_GetLighting(float *outStrength, float *outScatter)
{
  if (outStrength)
    *outStrength = s_lightingStrength;
  if (outScatter)
    *outScatter = s_scatterStrength;
}

static Vector3 ColorToVec3_Particle(Color c)
{
  return (Vector3){c.r * (1.0f / 255.0f), c.g * (1.0f / 255.0f), c.b * (1.0f / 255.0f)};
}

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
  if (s_lightingStrength < 0.0f)
    s_lightingStrength = 0.0f;
  else if (s_lightingStrength > 1.0f)
    s_lightingStrength = 1.0f;
  if (s_scatterStrength < 0.0f)
    s_scatterStrength = 0.0f;
  else if (s_scatterStrength > 2.0f)
    s_scatterStrength = 2.0f;

  {
    static float last[5] = {-999, -999, -999, -999, -999};
    const float now[5] = {s_lightingStrength, s_scatterStrength, s_normalBulge,
                          s_lightAzimuth, s_analyticUV};
    bool changed = false;
    for (int i = 0; i < 5; i++)
      if (fabsf(now[i] - last[i]) > 1e-4f)
        changed = true;
    if (changed)
    {
      for (int i = 0; i < 5; i++)
        last[i] = now[i];
      TraceLog(LOG_INFO,
               "PARTICLE F1: strength=%.2f scatter=%.2f bulge=%.2f azimuth=%.1f "
               "analyticUV=%.0f sunGain=%.2f ambGain=%.2f quality=%d%s",
               s_lightingStrength, s_scatterStrength, s_normalBulge, s_lightAzimuth,
               s_analyticUV, s_sunGain, s_ambientGain, (int)GfxQuality_Get(),
               (s_lightingStrength <= 0.0f) ? "  -> OFF (strength is 0)" : (GfxQuality_Get() <= GFX_LOW) ? "  -> OFF (needs GfxQuality >= MED)"
                                                                                                         : "  -> lit path ACTIVE");
    }
  }

  bool wantSoftParticles = (s_softFade > 0.0f && SceneTargets_GetDepthTexture().id != 0);
  bool wantVolume = ParticleSystem_HasVolumeParticles();
  if (s_lightingStrength <= 0.0f && !wantSoftParticles && !wantVolume)
    return;
  if (GfxQuality_Get() <= GFX_LOW && !wantSoftParticles && !wantVolume)
    return;

  if (!s_litShaderTried)
  {
    s_litShaderTried = true;
    s_litShader = ResourceManager_LoadShader("core/particles/shaders/particle_lit.vs",
                                             "core/particles/shaders/particle_lit.fs");
    if (s_litShader.id != 0)
    {
      s_locSunToLight = GetShaderLocation(s_litShader, "u_sunToLight");
      s_locSunColor = GetShaderLocation(s_litShader, "u_sunColor");
      s_locAmbient = GetShaderLocation(s_litShader, "u_ambient");
      s_locViewPos = GetShaderLocation(s_litShader, "viewPos");
      s_locLightStrength = GetShaderLocation(s_litShader, "u_lightingStrength");
      s_locBgLuma = GetShaderLocation(s_litShader, "u_bgLuma");
      s_locBgAdapt = GetShaderLocation(s_litShader, "u_bgAdapt");
      s_locScatterStrength = GetShaderLocation(s_litShader, "u_scatterStrength");
      s_locVfxCount = GetShaderLocation(s_litShader, "u_vfxLightCount");
      s_locVfxPos = GetShaderLocation(s_litShader, "u_vfxLightPos");
      s_locVfxColor = GetShaderLocation(s_litShader, "u_vfxLightColor");
      s_locVfxRadius = GetShaderLocation(s_litShader, "u_vfxLightRadius");
      s_locDebugNormal = GetShaderLocation(s_litShader, "u_debugNormal");
      s_locNormalBulge = GetShaderLocation(s_litShader, "u_normalBulge");
      s_locSunGain = GetShaderLocation(s_litShader, "u_sunGain");
      s_locAmbientGain = GetShaderLocation(s_litShader, "u_ambientGain");
      s_locLightAzimuth = GetShaderLocation(s_litShader, "u_lightAzimuth");
      s_locAnalyticUV = GetShaderLocation(s_litShader, "u_analyticUV");
      s_locSoftFade = GetShaderLocation(s_litShader, "u_softFade");
      s_locSoftDebug = GetShaderLocation(s_litShader, "u_softDebug");
      s_locAtlasGrid = GetShaderLocation(s_litShader, "u_atlasGrid");
      s_locEmissiveBoost = GetShaderLocation(s_litShader, "u_emissiveBoost");
      s_locVolumeSheet = GetShaderLocation(s_litShader, "u_volumeSheet");
      s_locRampLUT = GetShaderLocation(s_litShader, "u_rampLUT");
      s_locHeatGain = GetShaderLocation(s_litShader, "u_heatGain");
      s_locSmokeTint = GetShaderLocation(s_litShader, "u_smokeTint");
      s_locSmokeGain = GetShaderLocation(s_litShader, "u_smokeGain");
      s_locSixWayLighting = GetShaderLocation(s_litShader, "u_sixWayLighting");
      s_locSixWayTexB = GetShaderLocation(s_litShader, "u_sixWayTexB");
      s_locSixWayScattering = GetShaderLocation(s_litShader, "u_sixWayScattering");
      s_locSixWayAbsorption = GetShaderLocation(s_litShader, "u_sixWayAbsorption");
      s_locAmbientGround = GetShaderLocation(s_litShader, "u_ambientGround");
      s_locAmbientHorizon = GetShaderLocation(s_litShader, "u_ambientHorizon");
      s_locNormalTex = GetShaderLocation(s_litShader, "u_normalTex");
      s_locUseMotionVectors = GetShaderLocation(s_litShader, "u_useMotionVectors");
      s_locMotionTex = GetShaderLocation(s_litShader, "u_motionTex");
      s_locMotionWarp = GetShaderLocation(s_litShader, "u_motionWarp");
      s_locTime = GetShaderLocation(s_litShader, "u_time");
    }
    else
    {
      TraceLog(LOG_WARNING,
               "PARTICLE: particle_lit shader failed to load — particles stay unlit");
    }
  }

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
  Vector3 sunColor = Vector3Scale(ColorToVec3_Particle(Environment_GetSunColor()),
                                  Environment_GetSunIntensity());
  Vector3 ambient = ColorToVec3_Particle(Environment_GetAmbientColor());

  BeginShaderMode(s_litShader);
  s_litActive = true;

  SetShaderValue(s_litShader, s_locSunToLight, &sunToLight, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locSunColor, &sunColor, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locAmbient, &ambient, SHADER_UNIFORM_VEC3);
  Vector3 ambientGround = {ambient.x * 0.45f, ambient.y * 0.42f, ambient.z * 0.38f};
  Vector3 ambientHorizon = {ambient.x * 0.70f, ambient.y * 0.70f, ambient.z * 0.72f};
  if (s_locAmbientGround >= 0)
    SetShaderValue(s_litShader, s_locAmbientGround, &ambientGround, SHADER_UNIFORM_VEC3);
  if (s_locAmbientHorizon >= 0)
    SetShaderValue(s_litShader, s_locAmbientHorizon, &ambientHorizon, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locViewPos, &camera.position, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_litShader, s_locLightStrength, &s_lightingStrength, SHADER_UNIFORM_FLOAT);
  {
    static bool reg = false;
    if (!reg)
    {
      Tuning_RegisterFloat("particle_bg_adapt", &s_bgAdapt, 0.0f);
      reg = true;
    }
  }
  if (s_locBgAdapt >= 0)
    SetShaderValue(s_litShader, s_locBgAdapt, &s_bgAdapt, SHADER_UNIFORM_FLOAT);
  if (s_locBgLuma >= 0)
  {
    Texture2D bl = SceneTargets_GetBackgroundLuma();
    if (bl.id != 0)
      SetShaderValueTexture(s_litShader, s_locBgLuma, bl);
  }
  SetShaderValue(s_litShader, s_locScatterStrength, &s_scatterStrength, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locDebugNormal, &s_debugNormal, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locNormalBulge, &s_normalBulge, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locSunGain, &s_sunGain, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locAmbientGain, &s_ambientGain, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locLightAzimuth, &s_lightAzimuth, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_litShader, s_locAnalyticUV, &s_analyticUV, SHADER_UNIFORM_FLOAT);
  if (s_locTime >= 0)
  {
    float timeNow = TimeFX_Elapsed();
    SetShaderValue(s_litShader, s_locTime, &timeNow, SHADER_UNIFORM_FLOAT);
  }
  {
    float one = 1.0f;
    if (s_locEmissiveBoost >= 0)
      SetShaderValue(s_litShader, s_locEmissiveBoost, &one, SHADER_UNIFORM_FLOAT);
  }
  {
    Texture2D depthTex = SceneTargets_GetDepthTexture();
    float fade = (depthTex.id != 0 && s_softFade > 0.0f && s_locSoftFade >= 0)
                     ? s_softFade
                     : 0.0f;
    if (fade > 0.0f && depthTex.id != 0)
      SceneTargets_BindDepthForSoftParticles(s_litShader, PARTICLE_SOFT_DEPTH_SLOT);
    if (s_locSoftFade >= 0)
      SetShaderValue(s_litShader, s_locSoftFade, &fade, SHADER_UNIFORM_FLOAT);
    if (s_locSoftDebug >= 0)
      SetShaderValue(s_litShader, s_locSoftDebug, &s_softDebug, SHADER_UNIFORM_FLOAT);
    if (fade <= 0.0f && s_softDebug > 0.5f && depthTex.id != 0)
    {
      SceneTargets_BindDepthForSoftParticles(s_litShader, PARTICLE_SOFT_DEPTH_SLOT);
      s_softBound = true;
    }
    s_softBound = (fade > 0.0f || (s_softDebug > 0.5f && depthTex.id != 0));
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
  {
    float grid[2] = {1.0f, 1.0f};
    if (s_locAtlasGrid >= 0)
      SetShaderValue(s_litShader, s_locAtlasGrid, grid, SHADER_UNIFORM_VEC2);
  }
  {
    float off = 0.0f, one = 1.0f;
    if (s_locVolumeSheet >= 0)
      SetShaderValue(s_litShader, s_locVolumeSheet, &off, SHADER_UNIFORM_FLOAT);
    if (s_locHeatGain >= 0)
      SetShaderValue(s_litShader, s_locHeatGain, &one, SHADER_UNIFORM_FLOAT);
    if (s_locSmokeGain >= 0)
      SetShaderValue(s_litShader, s_locSmokeGain, &one, SHADER_UNIFORM_FLOAT);
    if (s_locSmokeTint >= 0)
    {
      float tint[3] = {0.32f, 0.29f, 0.27f};
      SetShaderValue(s_litShader, s_locSmokeTint, tint, SHADER_UNIFORM_VEC3);
    }
  }
  {
    float zero = 0.0f, one = 1.0f;
    if (s_locSixWayLighting >= 0)
      SetShaderValue(s_litShader, s_locSixWayLighting, &zero, SHADER_UNIFORM_FLOAT);
    if (s_locSixWayScattering >= 0)
      SetShaderValue(s_litShader, s_locSixWayScattering, &one, SHADER_UNIFORM_FLOAT);
    if (s_locSixWayAbsorption >= 0)
      SetShaderValue(s_litShader, s_locSixWayAbsorption, &one, SHADER_UNIFORM_FLOAT);
  }

  VFXLightData lights[PARTICLE_MAX_VFX_LIGHTS];
  int count = 0;
  VFXLight_GetActive(lights, &count, PARTICLE_MAX_VFX_LIGHTS);
  if (count > PARTICLE_MAX_VFX_LIGHTS)
    count = PARTICLE_MAX_VFX_LIGHTS;

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

static void ParticleLighting_SetStrength(float v)
{
  if (!s_litActive)
    return;
  rlDrawRenderBatchActive();
  SetShaderValue(s_litShader, s_locLightStrength, &v, SHADER_UNIFORM_FLOAT);
}

static void ParticleLighting_SetEmissive(float v)
{
  if (!s_litActive || s_locEmissiveBoost < 0)
    return;
  rlDrawRenderBatchActive();
  SetShaderValue(s_litShader, s_locEmissiveBoost, &v, SHADER_UNIFORM_FLOAT);
}

static void ParticleLighting_End(void)
{
  if (!s_litActive)
    return;
  if (s_softBound)
  {
    SceneTargets_UnbindSoftParticleDepth(PARTICLE_SOFT_DEPTH_SLOT);
    s_softBound = false;
  }
  EndShaderMode();
  s_litActive = false;
}

static inline unsigned int VFXPackColor(Color c)
{
  return ((unsigned int)c.r << 24) | ((unsigned int)c.g << 16) |
         ((unsigned int)c.b << 8) | (unsigned int)c.a;
}

static int s_perfBatches = 0;
static int s_perfQuads = 0;
static float s_perfLog = 0.0f;

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
    float invViewLen = 1.0f / viewLen;
    viewDir.x *= invViewLen;
    viewDir.y *= invViewLen;
    viewDir.z *= invViewLen;
  }

  Vector3 right = {camera.up.y * viewDir.z - camera.up.z * viewDir.y,
                   camera.up.z * viewDir.x - camera.up.x * viewDir.z,
                   camera.up.x * viewDir.y - camera.up.y * viewDir.x};

  float rightLen = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rightLen > 0.0f)
  {
    float invRightLen = 1.0f / rightLen;
    right.x *= invRightLen;
    right.y *= invRightLen;
    right.z *= invRightLen;
  }

  Vector3 up = {viewDir.y * right.z - viewDir.z * right.y,
                viewDir.z * right.x - viewDir.x * right.z,
                viewDir.x * right.y - viewDir.y * right.x};

  // Kiểm tra xem Camera hoặc danh sách Particle có thay đổi kể từ pass trước không.
  // Nhờ đó khi gọi DrawParticlesBody rồi DrawParticlesEmission trong cùng 1 frame,
  // ta chỉ tính vùng Soft-Depth và chạy Shell Sort đúng 1 lần duy nhất!
  bool camChanged = (Vector3DistanceSqr(camera.position, s_lastSortCamPos) > 1e-6f) ||
                    (Vector3DistanceSqr(camera.target, s_lastSortCamTarget) > 1e-6f);
  bool needSortAndBounds = s_depthSortDirty || camChanged;

  if (needSortAndBounds)
  {
    if (s_softFade > 0.0f)
    {
      Rectangle bounds = {0};
      bool hasBounds = false;
      float screenH = (float)GetScreenHeight();
      float halfFovy = camera.fovy * DEG2RAD * 0.5f;
      float projScale = screenH / fmaxf(2.0f * tanf(halfFovy), 1e-5f);

      for (int a = 0; a < s_activeCount; ++a)
      {
        ParticleInternal *p = &g_Particles[s_activeIds[a]];
        if (p->renderMode == 3 || p->trailOnly)
          continue;
        float dx = p->x - camera.position.x;
        float dy = p->y - camera.position.y;
        float dz = p->z - camera.position.z;
        // viewDir hướng từ target về camera, nên hạt ở phía trước camera có tích vô hướng âm
        float forwardDist = -(dx * viewDir.x + dy * viewDir.y + dz * viewDir.z);
        if (forwardDist <= 0.01f)
          continue;

        float distSq = dx * dx + dy * dy + dz * dz;
        float distance = sqrtf(distSq);
        Vector2 center = GetWorldToScreen((Vector3){p->x, p->y, p->z}, camera);

        float drawRadius = p->radius;
        if (p->radiusCurve)
        {
          float lr = Clamp(p->lifetime / p->maxLifetime, 0.0f, 1.0f);
          drawRadius *= SkillCurve_Eval(p->radiusCurve, 1.0f - lr);
        }
        float softDepthRadius = drawRadius * fmaxf(p->facingAspect, 1.0f);
        float radiusPx = softDepthRadius * projScale / distance;
        Rectangle r = {center.x - radiusPx, center.y - radiusPx, radiusPx * 2.0f, radiusPx * 2.0f};
        if (!hasBounds)
        {
          bounds = r;
          hasBounds = true;
        }
        else
        {
          float x1 = fmaxf(bounds.x + bounds.width, r.x + r.width);
          float y1 = fmaxf(bounds.y + bounds.height, r.y + r.height);
          bounds.x = fminf(bounds.x, r.x);
          bounds.y = fminf(bounds.y, r.y);
          bounds.width = x1 - bounds.x;
          bounds.height = y1 - bounds.y;
        }
      }
      /* bounds is built from GetWorldToScreen (window px); the snapshot it arms
         writes into the scene target, smaller than the window under a render
         scale. Without this the region was 1/scale too large and shifted, so a
         soft particle near the frame edge could read a stale depth row. */
      if (hasBounds)
        SceneTargets_RequestSoftDepthRegion(
            SceneTargets_WindowRectToTarget(bounds));
    }

    // Sắp xếp các hạt từ xa đến gần (Back-to-Front Depth Sorting)
    for (int a = 0; a < s_activeCount; a++)
    {
      int idx = s_activeIds[a];
      ParticleInternal *p = &g_Particles[idx];
      s_particleDepths[idx] = p->x * viewDir.x + p->y * viewDir.y + p->z * viewDir.z;
    }
    SortParticlesByDepth(s_activeIds, s_activeCount, s_particleDepths);

    for (int a = 0; a < s_activeCount; a++)
    {
      s_slotListIndex[s_activeIds[a]] = a;
    }

    s_lastSortCamPos = camera.position;
    s_lastSortCamTarget = camera.target;
    s_depthSortDirty = false;
  }

  ParticleLighting_Begin(camera);

  unsigned int curTex = 0xFFFFFFFFu;
  int curBlend = -1;
  int curUnlit = -1;
  int curGridC = -1, curGridR = -1;
  float curBoost = -1.0f;
  int curVolume = -1;
  unsigned int curRamp = 0xFFFFFFFFu;
  float curHeat = -1.0f;
  float curSmokeGain = -1.0f;
  unsigned int curSmokeTint = 0xFFFFFFFFu;
  int curSixWay = -1;
  unsigned int curSixWayTexB = 0xFFFFFFFFu;
  float curSixWayScat = -1.0f;
  float curSixWayAbs = -1.0f;
  int curSmokeSheet = -1;
  unsigned int curNormalTex = 0xFFFFFFFFu;
  unsigned int curMotionTex = 0xFFFFFFFFu;
  float curMotionWarp = -1.0f;
  float curStrength = -1.0f;

  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (layerFilter == 0 && p->blendMode != VFX_BLEND_ALPHA)
      continue;
    if (layerFilter == 1 && p->blendMode == VFX_BLEND_ALPHA)
      continue;
    if (p->renderMode == 3 || p->trailOnly)
      continue;

    unsigned int want = p->texId ? p->texId : texture.id;
    int wantGridC = (p->spriteAnim ? p->spriteAnim->cols : 1);
    int wantGridR = (p->spriteAnim ? p->spriteAnim->rows : 1);
    float wantBoost = p->emissiveBoost * s_emissiveBoost;
    float wantStrength = p->unlit ? 0.0f : (((p->sixWayLighting || p->smokeSheet) && s_lightingStrength <= 0.0f) ? 1.0f : s_lightingStrength);
    int drawBlend = (layerFilter == 0) ? VFX_BLEND_ALPHA : p->blendMode;
    unsigned int packedSmokeTint = VFXPackColor(p->smokeTint);

    if (want != curTex || drawBlend != curBlend || wantStrength != curStrength ||
        wantGridC != curGridC || wantGridR != curGridR || wantBoost != curBoost ||
        p->volumeSheet != curVolume || p->rampTexId != curRamp ||
        p->heatGain != curHeat || p->smokeGain != curSmokeGain ||
        packedSmokeTint != curSmokeTint ||
        p->sixWayLighting != curSixWay || p->sixWayTexBId != curSixWayTexB ||
        p->sixWayScattering != curSixWayScat || p->sixWayAbsorption != curSixWayAbs ||
        p->smokeSheet != curSmokeSheet || p->normalTexId != curNormalTex ||
        p->motionTexId != curMotionTex || p->motionWarpScale != curMotionWarp)
    {
      if (curTex != 0xFFFFFFFFu)
        rlEnd();
      s_perfBatches++;
      if (drawBlend != curBlend)
      {
        rlDrawRenderBatchActive();
        if (curBlend >= 0)
          EndBlendMode();
        BeginBlendMode(drawBlend == VFX_BLEND_ADDITIVE        ? BLEND_ADDITIVE
                       : drawBlend == VFX_BLEND_PREMULTIPLIED ? BLEND_ALPHA_PREMULTIPLY
                                                              : BLEND_ALPHA);
        rlDrawRenderBatchActive();
        curBlend = drawBlend;
      }
      if (p->volumeSheet != curVolume || p->smokeSheet != curSmokeSheet ||
          p->rampTexId != curRamp ||
          p->heatGain != curHeat || p->smokeGain != curSmokeGain ||
          packedSmokeTint != curSmokeTint)
      {
        rlDrawRenderBatchActive();
        float vs = p->smokeSheet ? 3.0f : (float)p->volumeSheet;
        float hg = p->heatGain;
        if (s_litActive && s_locVolumeSheet >= 0)
          SetShaderValue(s_litShader, s_locVolumeSheet, &vs, SHADER_UNIFORM_FLOAT);
        if (s_litActive && s_locHeatGain >= 0)
          SetShaderValue(s_litShader, s_locHeatGain, &hg, SHADER_UNIFORM_FLOAT);
        if (s_litActive && p->volumeSheet && p->rampTexId != 0 && s_locRampLUT >= 0)
        {
          Texture2D ramp = {0};
          ramp.id = p->rampTexId;
          ramp.width = 1;
          ramp.height = 1;
          ramp.mipmaps = 1;
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
          float st[3] = {(float)p->smokeTint.r * (1.0f / 255.0f),
                         (float)p->smokeTint.g * (1.0f / 255.0f),
                         (float)p->smokeTint.b * (1.0f / 255.0f)};
          SetShaderValue(s_litShader, s_locSmokeTint, st, SHADER_UNIFORM_VEC3);
        }
        curVolume = p->volumeSheet;
        curRamp = p->rampTexId;
        curHeat = p->heatGain;
        curSmokeGain = p->smokeGain;
        curSmokeTint = packedSmokeTint;
      }
      if (p->sixWayLighting != curSixWay || p->sixWayTexBId != curSixWayTexB ||
          p->sixWayScattering != curSixWayScat || p->sixWayAbsorption != curSixWayAbs)
      {
        rlDrawRenderBatchActive();
        float sw = (float)p->sixWayLighting;
        float scat = p->sixWayScattering;
        float absVal = p->sixWayAbsorption;
        if (s_litActive && s_locSixWayLighting >= 0)
          SetShaderValue(s_litShader, s_locSixWayLighting, &sw, SHADER_UNIFORM_FLOAT);
        if (s_litActive && s_locSixWayScattering >= 0)
          SetShaderValue(s_litShader, s_locSixWayScattering, &scat, SHADER_UNIFORM_FLOAT);
        if (s_litActive && s_locSixWayAbsorption >= 0)
          SetShaderValue(s_litShader, s_locSixWayAbsorption, &absVal, SHADER_UNIFORM_FLOAT);
        if (s_litActive && p->sixWayLighting > 1 && p->sixWayTexBId != 0 && s_locSixWayTexB >= 0)
        {
          Texture2D texB = {0};
          texB.id = p->sixWayTexBId;
          texB.width = 1;
          texB.height = 1;
          texB.mipmaps = 1;
          texB.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
          SetShaderValueTexture(s_litShader, s_locSixWayTexB, texB);
        }
        curSixWay = p->sixWayLighting;
        curSixWayTexB = p->sixWayTexBId;
        curSixWayScat = p->sixWayScattering;
        curSixWayAbs = p->sixWayAbsorption;
      }
      if (p->smokeSheet != curSmokeSheet || p->normalTexId != curNormalTex)
      {
        rlDrawRenderBatchActive();
        if (s_litActive && p->normalTexId != 0 && s_locNormalTex >= 0)
        {
          Texture2D nTex = {0};
          nTex.id = p->normalTexId;
          nTex.width = 1;
          nTex.height = 1;
          nTex.mipmaps = 1;
          nTex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
          SetShaderValueTexture(s_litShader, s_locNormalTex, nTex);
        }
        curSmokeSheet = p->smokeSheet;
        curNormalTex = p->normalTexId;
      }
      if (p->motionTexId != curMotionTex || p->motionWarpScale != curMotionWarp)
      {
        rlDrawRenderBatchActive();
        float useMotion = (p->motionTexId != 0 && p->motionWarpScale > 0.0f) ? 1.0f : 0.0f;
        float warpScale = p->motionWarpScale;
        if (s_litActive && s_locUseMotionVectors >= 0)
          SetShaderValue(s_litShader, s_locUseMotionVectors, &useMotion, SHADER_UNIFORM_FLOAT);
        if (s_litActive && s_locMotionWarp >= 0)
          SetShaderValue(s_litShader, s_locMotionWarp, &warpScale, SHADER_UNIFORM_FLOAT);
        if (s_litActive && p->motionTexId != 0 && s_locMotionTex >= 0)
        {
          Texture2D mTex = {0};
          mTex.id = p->motionTexId;
          mTex.width = 1;
          mTex.height = 1;
          mTex.mipmaps = 1;
          mTex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
          SetShaderValueTexture(s_litShader, s_locMotionTex, mTex);
        }
        curMotionTex = p->motionTexId;
        curMotionWarp = p->motionWarpScale;
      }
      if (wantStrength != curStrength)
      {
        ParticleLighting_SetStrength(wantStrength);
        curStrength = wantStrength;
        curUnlit = p->unlit;
      }
      if (wantBoost != curBoost)
      {
        ParticleLighting_SetEmissive((p->unlit || p->volumeSheet) ? wantBoost : 1.0f);
        curBoost = wantBoost;
      }
      if (wantGridC != curGridC || wantGridR != curGridR)
      {
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
      c.r = (unsigned char)(r > 255 ? 255 : (r < 0 ? 0 : r));
      int g = (int)((float)c.g * mul);
      c.g = (unsigned char)(g > 255 ? 255 : (g < 0 ? 0 : g));
      int b = (int)((float)c.b * mul);
      c.b = (unsigned char)(b > 255 ? 255 : (b < 0 ? 0 : b));
    }

    c = VFXContrast_ApplyColor(
        c, p->contrastProfile,
        p->blendMode != VFX_BLEND_ALPHA
            ? VFX_CONTRAST_EMISSION
            : VFX_CONTRAST_BODY);

    if (p->volumeSheet)
    {
      float heat = 1.0f;
      if (p->emissiveCurve)
        heat = SkillCurve_Eval(p->emissiveCurve, invRatio);
      if (heat < 0.0f)
        heat = 0.0f;
      else if (heat > 1.0f)
        heat = 1.0f;
      unsigned char h = (unsigned char)(heat * 255.0f);
      c.r = h;
      c.g = (p->volumeSheet >= 5 || p->volumeSheet == 2) ? (unsigned char)(invRatio * 255.0f) : h;
      c.b = (p->volumeSheet >= 5) ? (unsigned char)(p->colorStart.b) : h;
    }

    float drawRadius = p->radius;
    if (p->radiusCurve)
    {
      drawRadius *= SkillCurve_Eval(p->radiusCurve, invRatio);
    }

    // ── FEATURE 3: 3D MESH PARTICLE RENDERING (Niagara NE_Debris Parity) ─────
    if (p->meshModel.meshCount > 0)
    {
      if (curTex != 0xFFFFFFFFu)
      {
        rlEnd();
        curTex = 0xFFFFFFFFu;
      }
      rlDrawRenderBatchActive();

      rlPushMatrix();
      rlTranslatef(p->x, p->y, p->z);
      rlRotatef(p->meshRotation.z * RAD2DEG, 0.0f, 0.0f, 1.0f);
      rlRotatef(p->meshRotation.y * RAD2DEG, 0.0f, 1.0f, 0.0f);
      rlRotatef(p->meshRotation.x * RAD2DEG, 1.0f, 0.0f, 0.0f);
      rlScalef(drawRadius, drawRadius, drawRadius);

      for (int m = 0; m < p->meshModel.meshCount; m++)
      {
        Material mat = p->meshModel.materials[p->meshModel.meshMaterial[m]];
        Color origColor = mat.maps[MATERIAL_MAP_DIFFUSE].color;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        DrawMesh(p->meshModel.meshes[m], mat, MatrixIdentity());
        mat.maps[MATERIAL_MAP_DIFFUSE].color = origColor;
      }

      rlPopMatrix();
      rlDrawRenderBatchActive();
      // Khôi phục lại s_litShader sau khi DrawMesh ghi đè shader của material
      if (s_litActive && s_litShader.id != 0)
      {
        BeginShaderMode(s_litShader);
      }
      continue;
    }

    float rx = right.x * drawRadius, ry = right.y * drawRadius, rz = right.z * drawRadius;
    float ux = up.x * drawRadius, uy = up.y * drawRadius, uz = up.z * drawRadius;
    bool authoredAxisCross = false;
    Vector3 authoredCrossSideB = {0};

    // ── FEATURE 1 & 2: ADVANCED FACING MODES ──
    if (p->facingMode == VFX_FACING_CROSS_BILLBOARD)
    {
      Vector3 sideA;
      Vector3 particleView = Vector3Subtract(camera.position,
                                             (Vector3){p->x, p->y, p->z});
      float pvLenSq = Vector3LengthSqr(particleView);
      if (pvLenSq < 1e-8f)
        particleView = viewDir;
      else
        particleView = Vector3Scale(particleView, 1.0f / sqrtf(pvLenSq));
      authoredAxisCross = PS_AxisCrossBasis(p->facingDirection, particleView,
                                            camera.up, &sideA,
                                            &authoredCrossSideB);
      if (authoredAxisCross)
      {
        Vector3 axis = Vector3Normalize(p->facingDirection);
        rx = sideA.x * drawRadius;
        ry = sideA.y * drawRadius;
        rz = sideA.z * drawRadius;
        ux = axis.x * drawRadius * p->facingAspect;
        uy = axis.y * drawRadius * p->facingAspect;
        uz = axis.z * drawRadius * p->facingAspect;
      }
    }
    else if (p->facingMode == VFX_FACING_GROUND_PLANE)
    {
      float cosT = cosf(p->rotation);
      float sinT = sinf(p->rotation);

      rx = cosT * drawRadius;
      ry = 0.0f;
      rz = sinT * drawRadius;

      ux = -sinT * drawRadius;
      uy = 0.0f;
      uz = cosT * drawRadius;
    }
    else if (p->stretchStrength > 0.0f || p->facingMode == VFX_FACING_VELOCITY)
    {
      float vx = p->vx * right.x + p->vy * right.y + p->vz * right.z;
      float vy = p->vx * up.x + p->vy * up.y + p->vz * up.z;
      float screenSpeedSq = vx * vx + vy * vy;
      float minSpeed = p->stretchMinSpeed > 0.05f ? p->stretchMinSpeed : 0.05f;
      if (screenSpeedSq > minSpeed * minSpeed)
      {
        float screenSpeed = sqrtf(screenSpeedSq);
        float invSpeed = 1.0f / screenSpeed;
        float dirX = vx * invSpeed;
        float dirY = vy * invSpeed;

        Vector3 tangentDir = {
            right.x * dirX + up.x * dirY,
            right.y * dirX + up.y * dirY,
            right.z * dirX + up.z * dirY};
        Vector3 rightDir = {
            right.x * dirY - up.x * dirX,
            right.y * dirY - up.y * dirX,
            right.z * dirY - up.z * dirX};

        float stretchFactor = 1.0f + screenSpeed * (p->stretchStrength > 0.0f ? p->stretchStrength : 0.05f);
        if (stretchFactor > 3.5f)
          stretchFactor = 3.5f;

        rx = rightDir.x * drawRadius;
        ry = rightDir.y * drawRadius;
        rz = rightDir.z * drawRadius;

        float stretchedRadius = drawRadius * stretchFactor;
        ux = tangentDir.x * stretchedRadius;
        uy = tangentDir.y * stretchedRadius;
        uz = tangentDir.z * stretchedRadius;
      }
    }
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

      rx = rxRot;
      ry = ryRot;
      rz = rzRot;
      ux = uxRot;
      uy = uyRot;
      uz = uzRot;
    }

    if (!authoredAxisCross && p->facingAspect != 1.0f)
    {
      ux *= p->facingAspect;
      uy *= p->facingAspect;
      uz *= p->facingAspect;
    }

    SpriteAnimFrameSample sample = {
        .uv = {0.0f, 0.0f, 1.0f, 1.0f}, .offset = {0}, .scale = {1.0f, 1.0f}};
    SpriteAnimFrameSample sampleNext = sample;
    float fbBlend = 0.0f;
    if (p->spriteAnim)
    {
      float age = (p->maxLifetime - p->lifetime) * p->spriteAnimRate +
                  p->spriteAnimPhase;
      sample = SpriteAnim_CalculateFrameSampleBlend(p->spriteAnim, age,
                                                    &sampleNext, &fbBlend);
      {
        static float s_fbBlendOn = 1.0f;
        static float s_fbBlendMax = 220.0f;
        static bool s_fbBlendReg = false;
        if (!s_fbBlendReg)
        {
          s_fbBlendReg = true;
          Tuning_RegisterFloat("particle_fb_blend", &s_fbBlendOn, 1.0f);
          Tuning_RegisterFloat("particle_fb_blend_max", &s_fbBlendMax, 220.0f);
        }
        if (s_fbBlendOn <= 0.5f || (float)s_activeCount > s_fbBlendMax)
          fbBlend = 0.0f;
      }
      if (p->motionTexId != 0 && p->motionWarpScale > 0.0f)
      {
        c.b = (unsigned char)(fminf(fmaxf(fbBlend, 0.0f), 1.0f) * 255.0f);
        fbBlend = 0.0f;
      }
    }

#define PS_EMIT_QUAD(_sample, _a)                                          \
  do                                                                       \
  {                                                                        \
    if ((_a) > 0)                                                          \
    {                                                                      \
      const SpriteAnimFrameSample psSample = (_sample);                    \
      const Rectangle psUV = psSample.uv;                                  \
      const float psU0 = p->spriteFlipX ? psUV.x + psUV.width : psUV.x;    \
      const float psU1 = p->spriteFlipX ? psUV.x : psUV.x + psUV.width;    \
      const float psV0 = p->spriteFlipY ? psUV.y + psUV.height : psUV.y;   \
      const float psV1 = p->spriteFlipY ? psUV.y : psUV.y + psUV.height;   \
      const float psRx = rx * psSample.scale.x;                            \
      const float psRy = ry * psSample.scale.x;                            \
      const float psRz = rz * psSample.scale.x;                            \
      const float psUx = ux * psSample.scale.y;                            \
      const float psUy = uy * psSample.scale.y;                            \
      const float psUz = uz * psSample.scale.y;                            \
      const float psX = p->x - 2.0f * (rx * psSample.offset.x +            \
                                       ux * psSample.offset.y);            \
      const float psY = p->y - 2.0f * (ry * psSample.offset.x +            \
                                       uy * psSample.offset.y);            \
      const float psZ = p->z - 2.0f * (rz * psSample.offset.x +            \
                                       uz * psSample.offset.y);            \
      rlColor4ub(c.r, c.g, c.b, (unsigned char)(_a));                      \
      rlTexCoord2f(psU0, psV1);                                            \
      rlVertex3f(psX + psRx - psUx, psY + psRy - psUy, psZ + psRz - psUz); \
      rlTexCoord2f(psU0, psV0);                                            \
      rlVertex3f(psX + psRx + psUx, psY + psRy + psUy, psZ + psRz + psUz); \
      rlTexCoord2f(psU1, psV0);                                            \
      rlVertex3f(psX - psRx + psUx, psY - psRy + psUy, psZ - psRz + psUz); \
      rlTexCoord2f(psU1, psV1);                                            \
      rlVertex3f(psX - psRx - psUx, psY - psRy - psUy, psZ - psRz - psUz); \
    }                                                                      \
  } while (0)

    if (fbBlend > 0.001f)
    {
      PS_EMIT_QUAD(sample, (float)c.a * (1.0f - fbBlend));
      PS_EMIT_QUAD(sampleNext, (float)c.a * fbBlend);
      s_perfQuads += 2;
    }
    else
    {
      PS_EMIT_QUAD(sample, (float)c.a);
      s_perfQuads++;
    }

    if (p->facingMode == VFX_FACING_CROSS_BILLBOARD)
    {
      if (authoredAxisCross)
      {
        rx = authoredCrossSideB.x * drawRadius;
        ry = authoredCrossSideB.y * drawRadius;
        rz = authoredCrossSideB.z * drawRadius;
      }
      else
      {
        float tempRx = rx, tempRy = ry, tempRz = rz;
        rx = -uz;
        ry = uy;
        rz = ux;
        ux = tempRx;
        uy = tempRy;
        uz = tempRz;
      }

      if (fbBlend > 0.001f)
      {
        PS_EMIT_QUAD(sample, (float)c.a * (1.0f - fbBlend));
        PS_EMIT_QUAD(sampleNext, (float)c.a * fbBlend);
        s_perfQuads += 2;
      }
      else
      {
        PS_EMIT_QUAD(sample, (float)c.a);
        s_perfQuads++;
      }
    }
#undef PS_EMIT_QUAD
  }

  if (curTex != 0xFFFFFFFFu)
    rlEnd();
  if (curBlend >= 0)
  {
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlDrawRenderBatchActive();
  }

  ParticleLighting_End();

  {
    static float s_perfOn = 0.0f;
    static bool s_perfReg = false;
    if (!s_perfReg)
    {
      s_perfReg = true;
      Tuning_RegisterFloat("particle_perf_log", &s_perfOn, 0.0f);
    }
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
  int trailBlend = -1;
  for (int a = 0; a < s_activeCount; a++)
  {
    ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->trailLength > 0 && p->trailHistoryCount >= 2)
    {
      if (layerFilter == 0 && p->blendMode != VFX_BLEND_ALPHA)
        continue;
      if (layerFilter == 1 && p->blendMode == VFX_BLEND_ALPHA)
        continue;
      int want = (layerFilter == 0 || p->blendMode == VFX_BLEND_ALPHA) ? BLEND_ALPHA : BLEND_ADDITIVE;
      if (want != trailBlend)
      {
        if (trailBlend >= 0)
          EndBlendMode();
        BeginBlendMode(want);
        trailBlend = want;
      }
      RibbonPoint trailPoints[PS_TRAIL_MAX_RIBBON_PTS];
      int count = p->trailHistoryCount;
      if (count > p->trailLength)
        count = p->trailLength;

      float lifeRatio = Clamp(p->lifetime / p->maxLifetime, 0.0f, 1.0f);
      float invRatio = 1.0f - lifeRatio;
      Color baseColor = p->colorStart;
      if (p->gradient)
      {
        baseColor = ColorGradient_Sample(p->gradient, invRatio);
      }
      else
      {
        baseColor.a = (unsigned char)((int)p->colorStart.a * lifeRatio + (int)p->colorEnd.a * invRatio);
      }
      if (p->alphaCurve)
      {
        float mul = SkillCurve_Eval(p->alphaCurve, invRatio);
        int a_val = (int)((float)p->colorStart.a * mul);
        baseColor.a = (unsigned char)(a_val < 0 ? 0 : (a_val > 255 ? 255 : a_val));
      }

      float baseAlpha = (float)baseColor.a * (1.0f / 255.0f);

      int sub = p->trailSmooth ? PS_TRAIL_SUBDIV : 1;
      int outCount = (count - 1) * sub + 1;
      if (outCount > PS_TRAIL_MAX_RIBBON_PTS)
      {
        sub = 1;
        outCount = count;
      }

      // Tiền tính toán khoảng cách nút Centripetal Catmull-Rom một lần duy nhất cho cả dải Trail
      float intervals[8] = {0};
      if (sub > 1)
      {
        for (int k = 0; k < count - 1; k++)
        {
          intervals[k] = PS_KnotInterval(p->trailHistory[k], p->trailHistory[k + 1]);
        }
      }

      float invSub = 1.0f / (float)sub;
      float invCountMinus1 = 1.0f / (float)(count - 1);
      float baseWidth = p->radius * p->trailWidthRatio;

      for (int o = 0; o < outCount; o++)
      {
        float fh = (float)o * invSub;
        int h = (int)fh;
        float ft = fh - (float)h;
        if (h > count - 1)
        {
          h = count - 1;
          ft = 0.0f;
        }

        Vector3 pos;
        if (sub == 1 || ft <= 0.0001f)
        {
          pos = p->trailHistory[h];
        }
        else
        {
          int i0 = (h - 1 < 0) ? 0 : h - 1;
          int i1 = h;
          int i2 = (h + 1 > count - 1) ? count - 1 : h + 1;
          int i3 = (h + 2 > count - 1) ? count - 1 : h + 2;

          float dt01 = (i0 == i1) ? 1e-4f : intervals[i0];
          float dt12 = (i1 == i2) ? 1e-4f : intervals[i1];
          float dt23 = (i2 == i3) ? 1e-4f : intervals[i2];

          float t0 = 0.0f;
          float t1 = dt01;
          float t2 = t1 + dt12;
          float t3 = t2 + dt23;

          pos = PS_CatmullRomPrecomputed(p->trailHistory[i0], p->trailHistory[i1],
                                         p->trailHistory[i2], p->trailHistory[i3],
                                         t0, t1, t2, t3, ft);
        }

        trailPoints[o].position = pos;

        float ageRatio = fh * invCountMinus1;
        trailPoints[o].halfWidth = baseWidth * (1.0f - ageRatio * 0.7f);

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
  if (trailBlend >= 0)
    EndBlendMode();

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
    if ((p->volumeSheet || p->sixWayLighting || p->smokeSheet) && p->renderMode != 3 && !p->trailOnly)
      return true;
  }
  return false;
}

bool ParticleSystem_HasAdditiveParticles(void)
{
  for (int i = 0; i < s_activeCount; ++i)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[i]];
    if (p->blendMode == VFX_BLEND_ALPHA || p->renderMode == 3)
      continue;
    if (p->trailOnly && !(p->trailLength > 0 && p->trailHistoryCount >= 2))
      continue;
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

int ParticleSystem_CountSurfaceSamples(int emitterId)
{
  int count = 0;
  for (int a = 0; a < s_activeCount; ++a)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->emitterId == emitterId && p->renderMode == 3)
      ++count;
  }
  return count;
}

bool ParticleSystem_IsForceFieldInUse(const ForceField *field)
{
  if (!field)
    return false;
  for (int i = 0; i < s_activeCount; ++i)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[i]];
    if (!p->active)
      continue;
    const ForceField *arrival = p->travelPath ? p->travelPath->arrivalForceField : NULL;
    if (ParticleFieldReference_Matches(p->active, field, p->forceField, arrival))
      return true;
  }
  return false;
}

int ParticleSystem_GetSurfaceSamplesSpaced(int emitterId, ParticleSurfaceSample *outSamples, int maxSamples)
{
  if (!outSamples || maxSamples <= 0)
    return 0;
  int demand = ParticleSystem_CountSurfaceSamples(emitterId);
  int admitted = demand < maxSamples ? demand : maxSamples;
  int ordinal = 0, count = 0;
  for (int a = 0; a < s_activeCount && count < admitted; ++a)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->emitterId != emitterId || p->renderMode != 3)
      continue;
    if (ordinal++ != ParticleSurfaceSampling_Ordinal(count, admitted, demand))
      continue;
    outSamples[count++] = (ParticleSurfaceSample){{p->x, p->y, p->z}, p->radius};
  }
  return count;
}

int ParticleSystem_GetSurfaceSamples(int emitterId, ParticleSurfaceSample *outSamples, int maxSamples)
{
  if (!outSamples || maxSamples <= 0)
    return 0;
  int count = 0;
  for (int a = 0; a < s_activeCount && count < maxSamples; ++a)
  {
    const ParticleInternal *p = &g_Particles[s_activeIds[a]];
    if (p->emitterId != emitterId || p->renderMode != 3)
      continue;
    outSamples[count++] = (ParticleSurfaceSample){{p->x, p->y, p->z}, p->radius};
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

void ParticleSystem_SpawnGlow(ParticleConfig core)
{
  core.render.blendMode = VFX_BLEND_PREMULTIPLIED;
  core.render.unlit = 1;
  SpawnParticle(core);
}

static Texture2D s_defaultParticleSprite = {0};
static Texture2D s_elementSparkSprite = {0};
static Texture2D s_glowSprite = {0};
static Texture2D s_sparkCapsuleSprite = {0};

Texture2D ParticleSystem_DefaultSprite(void)
{
  if (s_defaultParticleSprite.id != 0)
    return s_defaultParticleSprite;
  const int N = 128;
  const float half = N * 0.5f;
  const float invHalf = 1.0f / half;
  const float radius = 0.84f;
  const float invFeather = 1.0f / 0.22f;
  Image img = GenImageColor(N, N, BLANK);
  Color *pixels = (Color *)img.data;
  for (int y = 0; y < N; ++y)
  {
    float py = ((float)y + 0.5f - half) * invHalf;
    for (int x = 0; x < N; ++x)
    {
      float px = ((float)x + 0.5f - half) * invHalf;
      float dist = sqrtf(px * px + py * py);
      float coverage = Clamp((radius - dist) * invFeather, 0.0f, 1.0f);
      float alpha = coverage * coverage * (3.0f - 2.0f * coverage);
      float core = Clamp(1.0f - dist * 2.0f, 0.0f, 1.0f);
      float whiteCore = core * core * (3.0f - 2.0f * core);
      pixels[y * N + x] = (Color){
          255,
          (unsigned char)(70.0f + 185.0f * whiteCore),
          (unsigned char)(8.0f + 172.0f * whiteCore),
          (unsigned char)(255.0f * alpha)};
    }
  }
  s_defaultParticleSprite = LoadTextureFromImage(img);
  SetTextureFilter(s_defaultParticleSprite, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
  return s_defaultParticleSprite;
}

Texture2D ParticleSystem_ElementSparkSprite(void)
{
  if (s_elementSparkSprite.id != 0)
    return s_elementSparkSprite;
  const int N = 128;
  const float half = N * 0.5f;
  const float invHalf = 1.0f / half;
  const float radius = 0.84f;
  const float invFeather = 1.0f / 0.22f;
  Image img = GenImageColor(N, N, BLANK);
  Color *pixels = (Color *)img.data;
  for (int y = 0; y < N; ++y)
  {
    float py = ((float)y + 0.5f - half) * invHalf;
    for (int x = 0; x < N; ++x)
    {
      float px = ((float)x + 0.5f - half) * invHalf;
      float dist = sqrtf(px * px + py * py);
      float coverage = Clamp((radius - dist) * invFeather, 0.0f, 1.0f);
      float alpha = coverage * coverage * (3.0f - 2.0f * coverage);
      float core = Clamp(1.0f - dist * 2.0f, 0.0f, 1.0f);
      float whiteCore = core * core * (3.0f - 2.0f * core);
      unsigned char value = (unsigned char)(72.0f + 183.0f * whiteCore);
      pixels[y * N + x] = (Color){value, value, value, (unsigned char)(255.0f * alpha)};
    }
  }
  s_elementSparkSprite = LoadTextureFromImage(img);
  SetTextureFilter(s_elementSparkSprite, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
  return s_elementSparkSprite;
}

Texture2D ParticleSystem_GlowSprite(void)
{
  if (s_glowSprite.id != 0)
    return s_glowSprite;
  const int N = 128;
  Image img = GenImageColor(N, N, BLANK);
  Color *pixels = (Color *)img.data;
  const float half = N * 0.5f;
  const float invHalf = 1.0f / half;
  for (int y = 0; y < N; y++)
  {
    float py = ((float)y + 0.5f - half) * invHalf;
    for (int x = 0; x < N; x++)
    {
      float px = ((float)x + 0.5f - half) * invHalf;
      float r2 = px * px + py * py;
      float v = (r2 >= 1.0f) ? 0.0f : (1.0f - r2);
      float a = v * v * v;
      pixels[y * N + x] = (Color){255, 255, 255, (unsigned char)(255.0f * a)};
    }
  }
  s_glowSprite = LoadTextureFromImage(img);
  SetTextureFilter(s_glowSprite, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
  return s_glowSprite;
}

Texture2D ParticleSystem_SparkCapsuleSprite(void)
{
  if (s_sparkCapsuleSprite.id != 0)
    return s_sparkCapsuleSprite;
  const int N = 128;
  const float half = N * 0.5f;
  const float invHalf = 1.0f / half;
  const float halfSegment = 0.62f;
  const float capsuleRadius = 0.22f;
  const float invFeather = 1.0f / 0.090f;
  const float invCoreRadius = 1.0f / (capsuleRadius * 0.72f);
  Image img = GenImageColor(N, N, BLANK);
  Color *pixels = (Color *)img.data;
  for (int y = 0; y < N; ++y)
  {
    float py = ((float)y + 0.5f - half) * invHalf;
    float qy = fabsf(py) - halfSegment;
    if (qy < 0.0f)
      qy = 0.0f;
    for (int x = 0; x < N; ++x)
    {
      float px = ((float)x + 0.5f - half) * invHalf;
      float dist = sqrtf(px * px + qy * qy);
      float coverage = Clamp((capsuleRadius - dist) * invFeather, 0.0f, 1.0f);
      float alpha = coverage * coverage * (3.0f - 2.0f * coverage);
      float core = Clamp(1.0f - dist * invCoreRadius, 0.0f, 1.0f);
      float whiteCore = core * core * (3.0f - 2.0f * core);
      pixels[y * N + x] = (Color){
          255,
          (unsigned char)(70.0f + 185.0f * whiteCore),
          (unsigned char)(8.0f + 172.0f * whiteCore),
          (unsigned char)(255.0f * alpha)};
    }
  }
  s_sparkCapsuleSprite = LoadTextureFromImage(img);
  SetTextureFilter(s_sparkCapsuleSprite, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
  return s_sparkCapsuleSprite;
}

void SpawnParticleOnMesh(const struct MeshAdjacency *adj, Matrix transform, ParticleConfig config)
{
  if (!adj || adj->count == 0)
    return;
  Vector3 localPos = MeshAdjacency_SampleEdge(adj);
  Vector3 worldPos = Vector3Transform(localPos, transform);
  config.position = worldPos;
  config.physics.position = worldPos;
  SpawnParticle(config);
}