#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/sprite_anim.h"
#include "core/vfx_config.h"
#include "raylib.h"
#include "core/ribbon_strip.h"
#include "core/vfx_light.h"

#define MAX_TRAIL_PARTICLES 500
#define TRAIL_HISTORY_COUNT 60

#define TRAIL_PROJECTILE_TAPER_POWER 1.2f
#define TRAIL_PROJECTILE_OUTER_WIDTH_MUL 1.3f
#define TRAIL_PROJECTILE_INNER_WIDTH_MUL 0.65f
#define TRAIL_PROJECTILE_OUTER_ALPHA_MAX 80.0f
#define TRAIL_PROJECTILE_SPAWN_OFFSET_MUL 0.45f
#define TRAIL_PROJECTILE_WOBBLE_FREQ 8.0f
#define TRAIL_PROJECTILE_RETARGET_DIST_SQR 0.04f
#define TRAIL_PROJECTILE_HIT_DIST_SQR 0.09f
#define TRAIL_PROJECTILE_ACCEL_RATE 1.5f
#define TRAIL_PROJECTILE_MAX_SPEED 30.0f
#define TRAIL_PROJECTILE_CURVE_RANGE 2.5f
#define TRAIL_PROJECTILE_WOBBLE_AMPLITUDE 3.5f
#define TRAIL_PROJECTILE_STEER_LERP_RATE 3.2f
#define TRAIL_PROJECTILE_QUAD_LENGTH_MUL 1.1f
#define TRAIL_PROJECTILE_QUAD_THICK_MUL 2.0f

#define TRAIL_WISP_WAVE_FREQ_MIN 10
#define TRAIL_WISP_WAVE_FREQ_MAX 20
#define TRAIL_WISP_WAVE_AMP_MIN 5
#define TRAIL_WISP_WAVE_AMP_MAX 18
#define TRAIL_WISP_DRAG_RATE 0.8f
#define TRAIL_WISP_WOBBLE_FREQ 15.0f
#define TRAIL_WISP_WRIGGLE_FREQ 15.0f
#define TRAIL_WISP_WRIGGLE_AMPLITUDE 12.0f
#define TRAIL_WISP_HEAD_TAPER_EDGE 0.2f
#define TRAIL_WISP_TAIL_TAPER_EDGE 0.5f

#define TRAIL_PORTAL_SPIN_DEG_PER_SEC 140.0f
#define TRAIL_PORTAL_SPAWN_GROW_TIME 0.12f
#define TRAIL_PORTAL_QUAD_SIZE_MUL 2.6f

#define TRAIL_FOLLOWER_IDLE_FADE_TIME 0.15f
#define TRAIL_FOLLOWER_FADE_RATE_PER_SEC 40.0f

typedef enum {
  TRAIL_TYPE_PROJECTILE = 0,
  TRAIL_TYPE_WISP = 1,
  TRAIL_TYPE_PORTAL = 2,
  TRAIL_TYPE_FOLLOWER = 3
} TrailType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);

typedef struct {
  TrailType type;
  Vector3 pos;
  Vector3 vel;
  float len;
  float thick;
  float trailLength;
  float life;
  Vector3 target;
  float initialAngle;
  float wobblePhase;
  float scale;
  Texture2D tex;
  Color tint;
  Shader shader;
  TrailUpdateCallback onUpdate;
  TrailDeathCallback onDeath;
  int ownerTag;
  // Per-instance TRAIL_TYPE_PROJECTILE overrides. >0 = override the global
  // TRAIL_PROJECTILE_* default; <=0 (default when TrailConfig is {0}) = use
  // the global macro default. Does NOT change the global macros themselves.
  float wobbleAmplitudeOverride;
  float curveRangeOverride;
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
  // Pool-eviction priority (CORE_ISSUES.md Item 12). Defaults to
  // VFX_PRIORITY_LOW (0) when TrailConfig is zero-initialized with {0} —
  // fully backward compatible. When the MAX_TRAIL_PARTICLES pool is full,
  // SpawnTrailEntity() evicts the lowest-priority active trail (ties broken
  // by shortest remaining lifetime) instead of rejecting the new spawn.
  VFXPriority priority;
  
  // Orbit parameters for TRAIL_TYPE_FOLLOWER
  float orbitRadius;
  float orbitSpeed;
  Vector3 orbitAxis;
  float orbitPhase;
  BlendMode blendMode;

  // Unified Config representation (Phase 3)
  VFX_GeneralConfig general;
  VFX_GeometryConfig geometry;
  VFX_PhysicsConfig physics;
  VFX_AnimationConfig animation;
  VFX_RenderConfig render;
} TrailConfig;

static inline void TrailConfig_Unify(TrailConfig *cfg) {
  // 1. Populate unified from legacy flat fields if legacy is set and unified is empty
  if (cfg->general.life == 0.0f && cfg->life != 0.0f) {
    cfg->general.life = cfg->life;
    cfg->general.priority = cfg->priority;
    cfg->general.tag = cfg->ownerTag;
  }
  if (cfg->geometry.scale == 0.0f && cfg->scale != 0.0f) {
    cfg->geometry.scale = cfg->scale;
    cfg->geometry.radius = 0.0f;
    cfg->geometry.width = cfg->thick;
    cfg->geometry.length = cfg->len;
  }
  if (cfg->physics.position.x == 0.0f && cfg->physics.position.y == 0.0f && cfg->physics.position.z == 0.0f) {
    cfg->physics.position = cfg->pos;
    cfg->physics.velocity = cfg->vel;
    cfg->physics.speed = 0.0f;
    cfg->physics.forceField = cfg->forceField;
  }
  if (cfg->animation.spriteAnim == NULL && cfg->spriteAnim != NULL) {
    cfg->animation.spriteAnim = cfg->spriteAnim;
    cfg->animation.radiusCurve = NULL;
    cfg->animation.speedCurve = NULL;
    cfg->animation.alphaCurve = NULL;
    cfg->animation.emissiveCurve = NULL;
  }
  if (cfg->render.gradient == NULL && cfg->gradient != NULL) {
    cfg->render.gradient = cfg->gradient;
    cfg->render.colorStart = cfg->tint;
    cfg->render.colorEnd = cfg->tint;
    cfg->render.tint = cfg->tint;
    cfg->render.shader = cfg->shader;
  } else if (cfg->render.tint.a == 0 && cfg->tint.a != 0) {
    cfg->render.tint = cfg->tint;
    cfg->render.colorStart = cfg->tint;
    cfg->render.colorEnd = cfg->tint;
    cfg->render.gradient = cfg->gradient;
    cfg->render.shader = cfg->shader;
  }

  // 2. Populate legacy flat fields from unified if unified is set and legacy is empty
  if (cfg->life == 0.0f && cfg->general.life != 0.0f) {
    cfg->life = cfg->general.life;
    cfg->priority = cfg->general.priority;
    cfg->ownerTag = cfg->general.tag;
  }
  if (cfg->scale == 0.0f && cfg->geometry.scale != 0.0f) {
    cfg->scale = cfg->geometry.scale;
    cfg->thick = cfg->geometry.width;
    cfg->len = cfg->geometry.length;
  }
  if (cfg->forceField == NULL && cfg->physics.forceField != NULL) {
    cfg->forceField = cfg->physics.forceField;
  }
  if (cfg->pos.x == 0.0f && cfg->pos.y == 0.0f && cfg->pos.z == 0.0f) {
    cfg->pos = cfg->physics.position;
    cfg->vel = cfg->physics.velocity;
  }
  if (cfg->spriteAnim == NULL && cfg->animation.spriteAnim != NULL) {
    cfg->spriteAnim = cfg->animation.spriteAnim;
  }
  if (cfg->gradient == NULL && cfg->render.gradient != NULL) {
    cfg->gradient = cfg->render.gradient;
  }
  if (cfg->tint.a == 0 && cfg->render.tint.a != 0) {
    cfg->tint = cfg->render.tint;
    cfg->shader = cfg->render.shader;
  }
}

// Đã tối ưu Struct Padding: Sắp xếp theo kích thước dữ liệu giảm dần
typedef struct {
  // 1. Con trỏ (Pointers) - 8 bytes mỗi biến
  TrailUpdateCallback onUpdate;
  TrailDeathCallback onDeath;
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
  // Non-NULL: tip position driven each frame by Vector3Transform(attachLocalOffset, *attachedTransform).
  // Caller owns the Matrix and must keep it valid for the trail's lifetime.
  const Matrix *attachedTransform;

  // 2. Mảng và Struct lớn (Vectors)
  Vector3 history[TRAIL_HISTORY_COUNT];
  Vector3 nodeVelocity[TRAIL_HISTORY_COUNT];

  Vector3 position;
  Vector3 velocity;
  Vector3 target;
  Vector3 driftVelocity;
  Vector3 axisOrigin; // CHỈ có ý nghĩa khi type == TRAIL_TYPE_FOLLOWER và
                      // forceField chứa layer FORCE_RADIAL_AXIS. Set bởi
                      // SetFollowerAxis().
  Vector3 axisDir; // Hướng trục, PHẢI là vector đơn vị khi truyền vào
                   // SetFollowerAxis().
  Vector3 attachLocalOffset; // Local-space offset transformed by attachedTransform each frame.

  // 3. Texture và Color
  Texture2D sprite;
  Color tint;
  Shader shader;

  // 4. Các biến thực (Floats) - 4 bytes
  float length;
  float thickness;
  float trailLength;
  float lifetime;
  float maxLifetime;
  float angle;
  float wobblePhase;
  float scale;
  float wobbleAmplitudeOverride;
  float curveRangeOverride;
  float timeSinceLastFollowerUpdate;
  float fadeAccumulator;
  float nodeRestLen;
  float orbitRadius;
  float orbitSpeed;
  float orbitPhase;
  Vector3 orbitAxis;

  // 5. Số nguyên và Enum (Int/Enum) - 4 bytes
  TrailType type;
  VFXPriority priority;
  int historyCount;
  int historyHead;
  int ownerTag;
  int nextFree;
  BlendMode blendMode;

  // 6. Kiểu Boolean - 1 byte
  bool active;
} TrailEntity;

void TrailSystem_SetGlobalTexture(Texture2D tex);
void InitTrailSystem(Shader defaultShader);
int SpawnTrailEntity(TrailConfig config);
TrailEntity *GetTrail(int id);
void KillTrail(int id);
void UpdateTrailSystem(float dt);
void DrawTrailEntities(Camera3D camera);
void UnloadTrailSystem(void);
int GetActiveTrailCount(void);
void TrailSystem_GetStats(int *active, int *max); // Item 32

void UpdateFollowerPosition(int id, Vector3 newTipPos);

// Attach a TRAIL_TYPE_FOLLOWER trail to an external Matrix (e.g. a bone
// transform). Each frame in UpdateTrailSystem the tip is recomputed as
// Vector3Transform(localOffset, *targetTransform). Pass localOffset={0,0,0}
// to track the matrix origin directly. The Matrix must stay valid for the
// trail's lifetime — typically a static field on the owning skill.
// Pass targetTransform=NULL to detach.
void Trail_AttachToTransform(int id, const Matrix *targetTransform,
                             Vector3 localOffset);

void Trail_SetFollowerOrbit(int id, float radius, float speed, Vector3 axis, float phase);

// Set trục động (axisOrigin + axisDir, axisDir PHẢI normalize trước khi
// gọi) dùng cho lực FORCE_RADIAL_AXIS trong forceField của entity FOLLOWER
// này. PHẢI gọi mỗi frame TRƯỚC UpdateTrailSystem() nếu trục di chuyển.
void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir);

#endif // TRAIL_SYSTEM_H