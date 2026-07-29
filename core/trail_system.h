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
// Ceiling on sub-frame node samples per frame, so a hitch cannot lay a hundred
// nodes in one update and burn the whole history on a single stutter.
#define TRAIL_SAMPLE_STEPS_MAX 6
#define TRAIL_CLOTH_CONSTRAIN_ITERS 2
// A node may bunch to this fraction of its rest spacing but no closer. Cloth
// gathers; nodes that COLLAPSE give a zero-length segment, and the tangent —
// a central difference over the neighbours — is then fabricated outright.
#define TRAIL_CLOTH_MIN_SPACING 0.60f

typedef enum {
  TRAIL_TYPE_PROJECTILE = 0,
  TRAIL_TYPE_WISP = 1,
  TRAIL_TYPE_PORTAL = 2,
  TRAIL_TYPE_FOLLOWER = 3
} TrailType;

// ── Layered ribbons ─────────────────────────────────────────────────────────
//
// A trail that reads as ENERGY is not one strip; it is a faint wide glow BEHIND
// a textured body with a hot line THROUGH it. The system used to hard-code
// exactly that idea at exactly one setting — an outer strip at 1.5x thickness
// and alpha 180, plus an optional inner strip at 0.4x, pure white, alpha 255 —
// which is why `disableInnerCore` exists: the one hard-coded core was wrong
// often enough to need an escape hatch.
//
// Declare the layers instead. Order is draw order, so index 0 is the backmost.
// Leave `layers` NULL and nothing changes: the legacy outer + inner pair is
// still what you get.
//
// THE TRAP THE HARD-CODED VERSION HID, and it costs a day when you meet it:
// **the structure must live in exactly ONE layer.** Several additive copies of
// the same textured pattern at different scroll phases average into something
// FLAT, and the wider layers throw the texture's edge detail outward as spikes.
// Give the body the texture; give the glow and the core `texture = NULL`.
#define TRAIL_MAX_LAYERS 4

typedef struct {
  float widthMul;    // x the entity's thickness
  float alphaMul;    // x the node's alpha
  float whiten;      // 0 = the node's own colour, 1 = white
  float scrollMul;   // x uvScrollSpeed — parallax between layers
  // Alpha *= pow(segRatio, headAlphaPow) when > 0. A layer that burns at the
  // head and is gone by mid-tail, so the trail has ONE bright spot instead of
  // three layers all bright in the same places.
  float headAlphaPow;
  // NULL = a flat untextured shape (what a glow or a core should be). Non-NULL
  // = this layer carries the structure. At most one layer should set it.
  const Texture2D *texture;
} TrailLayer;

typedef enum {
  TRAIL_WIDTH_ENVELOPE_UNIFORM = 0,
  TRAIL_WIDTH_ENVELOPE_TAPER_TAIL = 1,
  TRAIL_WIDTH_ENVELOPE_TAPER_BOTH = 2,
  TRAIL_WIDTH_ENVELOPE_PULSE = 3
} TrailWidthEnvelopeType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);
typedef bool (*TrailCollisionCheckCallback)(int trailId, Vector3 currentPos);

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

  // 5 New Upgrades configuration
  TrailCollisionCheckCallback collisionCheck;
  float uvTiling;
  float uvScrollSpeed;
  float minVertexDistance;
  TrailWidthEnvelopeType widthEnvelope;
  bool smoothSpline;
  bool disableInnerCore; // Set true to disable the extra bright core layer (PROJECTILE/FOLLOWER only)
  // blendMode defaults to BLEND_ADDITIVE. Set useCustomBlendMode=true to override
  // with any value including BLEND_ALPHA (=0), which cannot be detected via >0 check.
  bool useCustomBlendMode;

  const SkillCurve *widthCurve;
  const SkillCurve *alphaCurve;
  float distortionStrength;
  float distortionSpeed;

  // ── FOLLOWER extensions. Every one of these is inert at 0, so a config that
  // does not mention them behaves exactly as before. ───────────────────────
  //
  // Layered draw. NULL/0 = the legacy outer + inner pair.
  const TrailLayer *layers;
  int layerCount;

  // METRES OF RIBBON PER TEXTURE REPEAT. > 0 switches the UV from the legacy
  // `segRatio * uvTiling` to a MATERIAL coordinate, and the difference is not a
  // refinement — the legacy form has two defects that no amount of
  // `uvScrollSpeed` can cover:
  //   1. `segRatio` is the node's INDEX normalised over the strip, so the
  //      texture stretches and squashes as the trail grows and shortens.
  //   2. It is measured from the HEAD, which is moving. Once the history is
  //      full, a fixed piece of ribbon sees its own segRatio change at the
  //      emitter's speed, so most of the apparent scroll is the motion leaking
  //      in — locked to the emitter, and usually far too fast to read. See
  //      core/docs/LANDMINES.md, "A scroll built on a MOVING origin".
  // With this set, each node is stamped with the metres of path travelled when
  // it was laid, and `uvScrollSpeed` is then exactly the flow rate over the
  // cloth, in tiles per second, whatever the emitter is doing.
  float uvMetresPerTile;

  // Cloth. > 0 springs each node back toward where it was LAID, so the force
  // field perturbs the swept path instead of replacing it (without this a
  // FOLLOWER under any force field writhes free of its own trail).
  float nodeHomeSpring;
  float nodeHomeMaxDev;   // metres, ACROSS the path — the loose safety bound
  // ALONG the path, as a fraction of the node spacing. MUST be < 0.5: both ends
  // of a segment move, so the gap can close by twice this, and at 0.5 or above
  // two nodes can swap places. That is a FOLD, the polyline reverses, the strip
  // pinches into a wedge — and no distance constraint can undo it, because
  // distance is a scalar and a node that has passed THROUGH its neighbour just
  // reads as "slightly too close". 0.45 is a good value; 0 disables the bound.
  float nodeOrderFrac;

  // > 0: lay nodes at a fixed RATE with sub-frame interpolation, instead of one
  // per frame. One per frame makes the trail's length in metres a function of
  // the frame rate, and at 30 fps consecutive nodes land on top of each other.
  float sampleHz;
  // > 0 (metres/sec): a jump faster than this is a TELEPORT, and the trail is
  // cut and restarted rather than drawing a straight bridge through space the
  // emitter never swept.
  float teleportSpeed;
  // > 0 (metres/sec): below this the attach path stops laying nodes, so the idle
  // fade runs and the trail DECAYS. Without it an attached trail re-stamps its
  // idle timer every frame, and a weapon standing still holds a frozen
  // full-length ribbon forever — which is the single most "decal, not object"
  // thing a trail can do.
  float idleSpeed;

  // Ribbon orientation mode (anti-pinching). Default RIBBON_CAMERA_FACING for
  // backward compatibility. Set to RIBBON_WORLD_UP or RIBBON_FIXED_NORMAL for
  // normal-aligned trails that avoid camera-pinching artefacts.
  // fixedNormal is only used when ribbonMode == RIBBON_FIXED_NORMAL.
  RibbonMode ribbonMode;
  Vector3 fixedNormal;

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
    cfg->animation.alphaCurve = cfg->alphaCurve;
    cfg->animation.emissiveCurve = NULL;
    cfg->animation.widthCurve = cfg->widthCurve;
  }
  if (cfg->render.gradient == NULL && cfg->gradient != NULL) {
    cfg->render.gradient = cfg->gradient;
    cfg->render.colorStart = cfg->tint;
    cfg->render.colorEnd = cfg->tint;
    cfg->render.tint = cfg->tint;
    cfg->render.shader = cfg->shader;
    cfg->render.distortionStrength = cfg->distortionStrength;
    cfg->render.distortionSpeed = cfg->distortionSpeed;
  } else if (cfg->render.tint.a == 0 && cfg->tint.a != 0) {
    cfg->render.tint = cfg->tint;
    cfg->render.colorStart = cfg->tint;
    cfg->render.colorEnd = cfg->tint;
    cfg->render.gradient = cfg->gradient;
    cfg->render.shader = cfg->shader;
    cfg->render.distortionStrength = cfg->distortionStrength;
    cfg->render.distortionSpeed = cfg->distortionSpeed;
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
  if (cfg->widthCurve == NULL && cfg->animation.widthCurve != NULL) {
    cfg->widthCurve = cfg->animation.widthCurve;
  }
  if (cfg->alphaCurve == NULL && cfg->animation.alphaCurve != NULL) {
    cfg->alphaCurve = cfg->animation.alphaCurve;
  }
  if (cfg->gradient == NULL && cfg->render.gradient != NULL) {
    cfg->gradient = cfg->render.gradient;
  }
  if (cfg->distortionStrength == 0.0f && cfg->render.distortionStrength != 0.0f) {
    cfg->distortionStrength = cfg->render.distortionStrength;
    cfg->distortionSpeed = cfg->render.distortionSpeed;
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
  const SkillCurve *widthCurve;
  const SkillCurve *alphaCurve;
  // Non-NULL: tip position driven each frame by Vector3Transform(attachLocalOffset, *attachedTransform).
  // Caller owns the Matrix and must keep it valid for the trail's lifetime.
  const Matrix *attachedTransform;
  TrailCollisionCheckCallback collisionCheck;

  // 2. Mảng và Struct lớn (Vectors)
  Vector3 history[TRAIL_HISTORY_COUNT];
  Vector3 nodeVelocity[TRAIL_HISTORY_COUNT];
  // Where each node was LAID — the path itself, which the cloth is sprung back
  // toward. Only meaningful when nodeHomeSpring > 0.
  Vector3 nodeHome[TRAIL_HISTORY_COUNT];
  // Spacing at which each node was laid, metres. The order bound is a fraction
  // of THIS, not an absolute distance, which is the whole point of it.
  float nodeRest[TRAIL_HISTORY_COUNT];
  // The MATERIAL coordinate: metres of emitter path when this node was laid.
  // Stamped once, never revisited. Only meaningful when uvMetresPerTile > 0.
  float nodeUV[TRAIL_HISTORY_COUNT];

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
  Vector3 fixedNormal; // Normal vector for RIBBON_FIXED_NORMAL mode.

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
  float uvTiling;
  float uvScrollSpeed;
  float uvScrollOffset;
  float minVertexDistance;
  float distortionStrength;
  float distortionSpeed;
  float uvMetresPerTile;
  float laidDist;        // running total the nodeUV stamps come from, metres
  float nodeHomeSpring;
  float nodeHomeMaxDev;
  float nodeOrderFrac;
  float sampleHz;
  float sampleAcc;
  float teleportSpeed;
  float idleSpeed;
  Vector3 prevAttachPos; // last frame's tip, for sub-frame interpolation
  Vector3 lateralOffset; // world-space offset added to the attach point
  bool hasPrevAttach;

  // 5. Số nguyên và Enum (Int/Enum) - 4 bytes
  TrailType type;
  VFXPriority priority;
  int historyCount;
  int historyHead;
  int ownerTag;
  int nextFree;
  BlendMode blendMode;
  TrailWidthEnvelopeType widthEnvelope;
  RibbonMode ribbonMode;
  const TrailLayer *layers;
  int layerCount;

  // 6. Kiểu Boolean - 1 byte
  bool active;
  bool smoothSpline;
  bool disableInnerCore;
  bool useCustomBlendMode;
  bool frozen;
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

// A WORLD-space offset added to the attach point before the node is laid, for a
// bundle of trails that must spread apart along an axis the caller works out per
// frame — a swing-plane normal, say, which is not expressible as a fixed local
// offset because it is derived from the path the trail has already travelled.
// Call before UpdateTrailSystem(). Zero (the default) is no offset.
void Trail_SetLateralOffset(int id, Vector3 worldOffset);

// Hold a FOLLOWER's shape completely still — no new nodes, no cloth step, the
// head left where it is — while the UV scroll keeps running. `elapsed` for the
// flow is unaffected, so THE ONLY THING THAT MOVES IS THE FLOW.
//
// This exists because "is the energy actually flowing?" cannot be answered by
// looking at a trail that is simultaneously being swung: a moving shape with a
// moving texture and a moving shape with a painted-on one look the same. Freeze
// the shape and the question answers itself. Debug instrument, not a gameplay
// pause — a frozen trail still ages and still dies on its lifetime.
void Trail_SetFrozen(int id, bool frozen);

#endif // TRAIL_SYSTEM_H