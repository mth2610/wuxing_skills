#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "force_field.h"
#include "raylib.h"
#include "ribbon_strip.h"

#define MAX_TRAIL_PARTICLES 500
#define TRAIL_HISTORY_COUNT 20

typedef enum {
  TRAIL_TYPE_PROJECTILE = 0,
  TRAIL_TYPE_WISP = 1,
  TRAIL_TYPE_PORTAL = 2
} TrailType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);

// Đã tối ưu Struct Padding: Sắp xếp theo kích thước dữ liệu giảm dần
typedef struct {
  // 1. Con trỏ (Pointers) - 8 bytes mỗi biến
  TrailUpdateCallback onUpdate;
  TrailDeathCallback  onDeath;
  const ForceField   *forceField; // NULL = không dùng; set sau khi spawn qua GetTrail()

  // 2. Mảng và Struct lớn (Vectors)
  Vector3 history[TRAIL_HISTORY_COUNT];
  Vector3 position;
  Vector3 velocity;
  Vector3 target;

  // 3. Texture và Color
  Texture2D sprite;
  Color tint;

  // 4. Các biến thực (Floats) - 4 bytes
  float length;
  float thickness;
  float lifetime;
  float maxLifetime;
  float angle;
  float wobblePhase;
  float scale;

  // 5. Số nguyên và Enum (Int/Enum) - 4 bytes
  TrailType type;
  int historyCount;
  int historyHead; // MỚI: Con trỏ trỏ đến phần tử mới nhất (dùng cho Ring
                   // Buffer)
  int ownerTag;

  // 6. Kiểu Boolean - 1 byte (Đặt cuối để tránh lỗ hổng bộ nhớ - Memory
  // Padding)
  bool active;
} TrailEntity;

void InitTrailSystem(void);
int SpawnTrailEntity(TrailType type, Vector3 pos, Vector3 vel, float len,
                     float thick, float life, Vector3 target,
                     float initialAngle, float wobblePhase, float scale,
                     Texture2D tex, Color tint, TrailUpdateCallback onUpdate,
                     TrailDeathCallback onDeath, int ownerTag);
TrailEntity *GetTrail(int id);
void KillTrail(int id);
void UpdateTrailSystem(float dt);
void DrawTrailEntities(Camera3D camera);
void UnloadTrailSystem(void);

#endif // TRAIL_SYSTEM_H