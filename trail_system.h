#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "force_field.h"
#include "raylib.h"
#include "ribbon_strip.h"

#define MAX_TRAIL_PARTICLES 500
#define TRAIL_HISTORY_COUNT 20

// --- Named constants (thay magic numbers theo SKILL_STANDARD.md) ---
#define TRAIL_PROJECTILE_TAPER_POWER 1.2f
#define TRAIL_PROJECTILE_OUTER_WIDTH_MUL 1.3f
#define TRAIL_PROJECTILE_INNER_WIDTH_MUL 0.65f
#define TRAIL_PROJECTILE_OUTER_ALPHA_MAX 80.0f
#define TRAIL_PROJECTILE_SPAWN_OFFSET_MUL 0.45f
#define TRAIL_PROJECTILE_WOBBLE_FREQ 8.0f
#define TRAIL_PROJECTILE_RETARGET_DIST_SQR 400.0f // sqrt = 20 units
#define TRAIL_PROJECTILE_HIT_DIST_SQR 900.0f      // sqrt = 30 units
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

// Các constant cho TRAIL_TYPE_FOLLOWER
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

// STRUCT CẤU HÌNH: Giải quyết triệt để lỗi đếm tham số của Compiler
typedef struct {
  TrailType type;
  Vector3 pos;
  Vector3 vel;
  float len;
  float thick;
  float life;
  Vector3 target;
  float initialAngle;
  float wobblePhase;
  float scale;
  Texture2D tex;
  Color tint;
  Shader shader; // Shader riêng cho entity này (vd: shader sword khác shader
                 // lửa). Truyền (Shader){0} (id=0) để dùng shader mặc định
                 // của hệ thống (xem InitTrailSystem). Người gọi (skill)
                 // tự LoadShader() một lần lúc init skill, KHÔNG load lại
                 // mỗi lần spawn - trail_system chỉ giữ tham chiếu, không
                 // sở hữu lifetime của shader này.
  TrailUpdateCallback onUpdate;
  TrailDeathCallback onDeath;
  int ownerTag;
  const ForceField *forceField; // NULL = path thẳng đuột theo input ngoài.
                                // Khác NULL = path bị lệch thêm theo force.
} TrailConfig;

// Đã tối ưu Struct Padding: Sắp xếp theo kích thước dữ liệu giảm dần
typedef struct {
  // 1. Con trỏ (Pointers) - 8 bytes mỗi biến
  TrailUpdateCallback onUpdate;
  TrailDeathCallback onDeath;
  const ForceField *forceField;

  // 2. Mảng và Struct lớn (Vectors)
  Vector3 history[TRAIL_HISTORY_COUNT];
  Vector3 position;
  Vector3 velocity;
  Vector3 target;
  Vector3 driftVelocity; // Dùng riêng cho TRAIL_TYPE_FOLLOWER (Cách A)

  // 3. Texture và Color
  Texture2D sprite;
  Color tint;
  Shader shader; // Copy từ TrailConfig.shader lúc spawn. (Shader){0} nghĩa
                 // là "dùng default shader của hệ thống".

  // 4. Các biến thực (Floats) - 4 bytes
  float length;
  float thickness;
  float lifetime;
  float maxLifetime;
  float angle;
  float wobblePhase;
  float scale;
  float timeSinceLastFollowerUpdate;
  float fadeAccumulator;

  // 5. Số nguyên và Enum (Int/Enum) - 4 bytes
  TrailType type;
  int historyCount;
  int historyHead;
  int ownerTag;
  int nextFree; // Free-list O(1): index slot trống kế tiếp khi !active.
                // Khi active=true, giá trị này không có ý nghĩa (đừng đọc).

  // 6. Kiểu Boolean - 1 byte
  bool active;
} TrailEntity;

// defaultShader: shader dùng cho mọi trail KHÔNG chỉ định shader riêng
// (TrailConfig.shader.id == 0). Mỗi skill có thể tự LoadShader() shader
// riêng của mình (vd metalSwordShader, fireTrailShader) và truyền vào
// TrailConfig.shader lúc gọi SpawnTrailEntity - trail_system không tự
// load/unload các shader đó, người gọi tự quản lý lifetime.
// Truyền (Shader){0} nếu chưa có default, miễn mọi entity sau đó đều tự
// cấp shader riêng qua TrailConfig.shader.
void InitTrailSystem(Shader defaultShader);
int SpawnTrailEntity(TrailConfig config); // LUÔN LUÔN CHỈ CÓ 1 THAM SỐ
TrailEntity *GetTrail(int id);

// KillTrail: đánh dấu chết NGAY (active=false, trả slot về free-list tức
// thì) và gọi onDeath ngay tại đây. Không còn kiểu "chết trễ 1 frame" như
// bản cũ (set lifetime=0 rồi chờ UpdateTrailSystem dọn) - tránh trường hợp
// code gọi GetTrail(id)->active ngay sau KillTrail mà vẫn thấy true.
void KillTrail(int id);

void UpdateTrailSystem(float dt);
void DrawTrailEntities(Camera3D camera);

// UnloadTrailSystem: KHÔNG unload defaultShader hay bất kỳ shader nào
// được truyền qua TrailConfig.shader - người gọi (skill) tự chịu trách
// nhiệm UnloadShader() shader của mình trong UnloadXSkill() của họ.
void UnloadTrailSystem(void);

// Số trail đang active hiện tại - dùng để debug/HUD, O(1) (không quét pool).
int GetActiveTrailCount(void);

// Cập nhật vị trí tip cho 1 FOLLOWER entity - PHẢI gọi mỗi frame TRƯỚC
// UpdateTrailSystem() nếu muốn ribbon tiếp tục mọc dài theo vị trí mới.
// Không gọi trong 1 frame nào đó -> entity coi như "idle" frame đó, sau
// TRAIL_FOLLOWER_IDLE_FADE_TIME giây liên tục không được gọi sẽ bắt đầu
// rút ngắn ribbon dần (xem UpdateTrailSystem). Không có hiệu lực / no-op
// nếu id không tồn tại hoặc không phải TRAIL_TYPE_FOLLOWER.
void UpdateFollowerPosition(int id, Vector3 newTipPos);

#endif // TRAIL_SYSTEM_H