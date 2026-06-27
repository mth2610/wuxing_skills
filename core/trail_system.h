#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/sprite_anim.h"
#include "raylib.h"
#include "core/ribbon_strip.h"

#define MAX_TRAIL_PARTICLES 500
#define TRAIL_HISTORY_COUNT 60

#define TRAIL_PROJECTILE_TAPER_POWER 1.2f
#define TRAIL_PROJECTILE_OUTER_WIDTH_MUL 1.3f
#define TRAIL_PROJECTILE_INNER_WIDTH_MUL 0.65f
#define TRAIL_PROJECTILE_OUTER_ALPHA_MAX 80.0f
#define TRAIL_PROJECTILE_SPAWN_OFFSET_MUL 0.45f
#define TRAIL_PROJECTILE_WOBBLE_FREQ 8.0f
#define TRAIL_PROJECTILE_RETARGET_DIST_SQR 400.0f
#define TRAIL_PROJECTILE_HIT_DIST_SQR 900.0f
#define TRAIL_PROJECTILE_ACCEL_RATE 150.0f
#define TRAIL_PROJECTILE_MAX_SPEED 600.0f
#define TRAIL_PROJECTILE_CURVE_RANGE 250.0f
#define TRAIL_PROJECTILE_WOBBLE_AMPLITUDE 350.0f
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
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;
} TrailConfig;

// Đã tối ưu Struct Padding: Sắp xếp theo kích thước dữ liệu giảm dần
typedef struct {
  // 1. Con trỏ (Pointers) - 8 bytes mỗi biến
  TrailUpdateCallback onUpdate;
  TrailDeathCallback onDeath;
  const ForceField *forceField;
  const ColorGradient *gradient;
  const SpriteAnim *spriteAnim;

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
  float timeSinceLastFollowerUpdate;
  float fadeAccumulator;
  float nodeRestLen;

  // 5. Số nguyên và Enum (Int/Enum) - 4 bytes
  TrailType type;
  int historyCount;
  int historyHead;
  int ownerTag;
  int nextFree;

  // 6. Kiểu Boolean - 1 byte
  bool active;
} TrailEntity;

void InitTrailSystem(Shader defaultShader);
int SpawnTrailEntity(TrailConfig config);
TrailEntity *GetTrail(int id);
void KillTrail(int id);
void UpdateTrailSystem(float dt);
void DrawTrailEntities(Camera3D camera);
void UnloadTrailSystem(void);
int GetActiveTrailCount(void);

void UpdateFollowerPosition(int id, Vector3 newTipPos);

// Set trục động (axisOrigin + axisDir, axisDir PHẢI normalize trước khi
// gọi) dùng cho lực FORCE_RADIAL_AXIS trong forceField của entity FOLLOWER
// này. PHẢI gọi mỗi frame TRƯỚC UpdateTrailSystem() nếu trục di chuyển.
void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir);

#endif // TRAIL_SYSTEM_H