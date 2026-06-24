#include "fluid_skill.h"
#include "particle_system.h"
#include "path_spline.h"
#include "raymath.h"
#include "rlgl.h"
#include "skill_manager.h"
#include "utils_math.h"
#include <math.h>

#define MAX_EMITTERS 10

#define WATER_TRAVEL_SPEED 1.6f
#define WATER_BASE_RADIUS 16.0f

#define MIST_SPAWN_RATE 45.0f
#define GRAVITY_Y -650.0f
#define FLUID_DRAG_SPLASH 3.0f
#define FLUID_DRAG_MIST 4.5f

// Độ phân giải lưới quả cầu. VS displacement che hầu hết đường thô,
// nên 40x40 là đủ mượt. Giảm xuống 28 nếu cần squeeze thêm FPS.
#define SPHERE_RINGS 40
#define SPHERE_COLUMNS 40

extern Camera3D camera;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 p1, p2;
  float progress;
  float sizeScale;
  float spawnAccumulator;
  Vector3 currentPos;
} FluidEmitter;

static FluidEmitter emitters[MAX_EMITTERS];
static Shader fluidShader;
static int timeLoc; // u_time dùng chung cho cả VS và FS (1 uniform, 1 lần set)
static int viewPosLoc;

static inline float ClampSizeScale(float scale) {
  return Clamp(scale, 0.2f, 3.0f);
}

static void TriggerFluidImpact(Vector3 pos, float sizeScale) {
  // Tăng số lượng hạt splash để tia nước nhìn dày dặn, rõ nét hơn
  int splashCount = GetRandomValue(30, 45) * sizeScale;

  for (int s = 0; s < splashCount; s++) {
    // 1. Hướng dạt ngang (vòng tròn 360 độ quanh điểm va chạm)
    float angle = Random01() * PI * 2.0f;

    // 2. SỬA TẠI ĐÂY: Thay vì góc pitch tự do từ -90 đến 90 độ,
    // ta ép góc pitch chỉ từ 15 đến 65 độ (0.26 rad đến 1.14 rad) hướng lên
    // trên.
    float pitch = Math_Mix(15.0f * DEG2RAD, 65.0f * DEG2RAD, Random01());

    // 3. Tốc độ văng mạnh mẽ hơn khi va chạm
    float speed = Math_Mix(350.0f, 700.0f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;

    // Tính toán lại Vector vận tốc dựa trên góc pitch đã ép hướng lên trên
    cfg.velocity = (Vector3){
        cosf(angle) * speed * cosf(pitch), // Lực văng dạt ngang trục X
        sinf(pitch) * speed, // Lực hất ngược lên trên trục Y (luôn dương)
        sinf(angle) * speed * cosf(pitch) // Lực văng dạt ngang trục Z
    };

    // Trọng lực kéo nước rơi xuống lại tạo hình vòng cung splash
    cfg.viscosity = 1.2f; // Độ nhớt càng cao, tia nước văng ra sẽ gom cụm, đặc
                          // quánh mượt mà hơn (Thử trong khoảng 0.2f đến 3.0f)
    cfg.force = (Vector3){0.0f, GRAVITY_Y * 1.3f, 0.0f};
    cfg.drag = FLUID_DRAG_SPLASH * 0.7f; // Giảm bớt drag để giọt nước văng đi
                                         // xa hơn thay vì khựng lại thành sương

    // Kích thước hạt ngẫu nhiên từ nhỏ đến lớn để tạo các tia nước bắn ra sinh
    // động
    cfg.radius = Math_Mix(1.5f, 5.0f, Random01()) * sizeScale * 3.5f;
    cfg.lifetime = Math_Mix(0.3f, 0.9f, Random01());

    // Màu sắc: Giữ màu nước xanh trong suốt
    cfg.colorStart = (Color){180, 230, 255, 240};
    cfg.colorEnd = (Color){50, 130, 200, 0};
    cfg.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;

    SpawnParticle(cfg);
  }
}

static inline float WaterBlobOffset(float theta, float phi, float time) {
  // Thành phần 1: Tạo các múi phình ra xẹp vào theo cả chiều dọc và ngang
  // Nhân hàm không gian (theta, phi) với hàm thời gian (time) để tạo sóng dừng
  float wave1 = sinf(theta * 3.0f) * cosf(phi * 2.0f) * sinf(time * 5.0f);

  // Thành phần 2: Thêm chi tiết bất đối xứng để khối nước trông tự nhiên hơn
  float wave2 = sinf(theta * 4.0f + phi * 3.0f) * cosf(time * 4.2f);

  // Thành phần 3: Nhịp đập tổng thể nhỏ và nhanh hơn (tạo độ rung nhẹ trên bề
  // mặt)
  float wave3 = cosf(theta * 2.0f - phi * 4.0f) * sinf(time * 6.5f);

  // Tổng hợp lại với các trọng số biên độ khác nhau
  return wave1 * 0.12f + wave2 * 0.08f + wave3 * 0.05f;
}

// HÀM VẼ KHỐI CẦU 3D QUA TẦNG RLGL
// TỐI ƯU: Pre-compute toàn bộ sin/cos một lần trước vòng lặp vẽ.
// Từ 40*40*4*2 = 12.800 lần gọi trig/frame xuống còn (41+41)*2 = 164 lần.
static void RenderCustom3DSphere(Vector3 center, float radius, float blobAmount,
                                 float time) {
  // Bảng góc pre-computed — giữ nguyên của bạn
  float sinTheta[SPHERE_RINGS + 1], cosTheta[SPHERE_RINGS + 1];
  float sinPhi[SPHERE_COLUMNS + 1], cosPhi[SPHERE_COLUMNS + 1];

  for (int i = 0; i <= SPHERE_RINGS; i++) {
    float theta = (float)i * PI / (float)SPHERE_RINGS;
    sinTheta[i] = sinf(theta);
    cosTheta[i] = cosf(theta);
  }
  for (int j = 0; j <= SPHERE_COLUMNS; j++) {
    float phi = (float)j * (2.0f * PI) / (float)SPHERE_COLUMNS;
    sinPhi[j] = sinf(phi);
    cosPhi[j] = cosf(phi);
  }

  rlPushMatrix();
  rlTranslatef(center.x, center.y, center.z);
  rlBegin(RL_QUADS);

  for (int i = 0; i < SPHERE_RINGS; i++) {
    float st1 = sinTheta[i], ct1 = cosTheta[i];
    float st2 = sinTheta[i + 1], ct2 = cosTheta[i + 1];

    float theta1 = (float)i * PI / (float)SPHERE_RINGS;
    float theta2 = (float)(i + 1) * PI / (float)SPHERE_RINGS;

    float vt1 = (float)i / (float)SPHERE_RINGS;
    float vt2 = (float)(i + 1) / (float)SPHERE_RINGS;

    for (int j = 0; j < SPHERE_COLUMNS; j++) {
      float cp1 = cosPhi[j], sp1 = sinPhi[j];
      float cp2 = cosPhi[j + 1], sp2 = sinPhi[j + 1];

      // Tọa độ góc dùng để tính toán nhiễu sóng
      float phi1 = (float)j * (2.0f * PI) / (float)SPHERE_COLUMNS;

      // SỬA TẠI ĐÂY: Nếu cột kế tiếp là cột cuối cùng (khép góc),
      // ta ép góc phi2 về 0 để đồng bộ hoàn hảo với cột đầu tiên.
      float phi2 = (j + 1 == SPHERE_COLUMNS)
                       ? 0.0f
                       : (float)(j + 1) * (2.0f * PI) / (float)SPHERE_COLUMNS;

      float u1 = (float)j / (float)SPHERE_COLUMNS;
      float u2 = (float)(j + 1) / (float)SPHERE_COLUMNS;

      // Tính bán kính biến dạng dựa trên góc đã đồng bộ biên
      float r1 =
          radius * (1.0f + WaterBlobOffset(theta1, phi1, time) * blobAmount);
      float r2 =
          radius * (1.0f + WaterBlobOffset(theta1, phi2, time) * blobAmount);
      float r3 =
          radius * (1.0f + WaterBlobOffset(theta2, phi2, time) * blobAmount);
      float r4 =
          radius * (1.0f + WaterBlobOffset(theta2, phi1, time) * blobAmount);

      // Đỉnh 1 (theta1, phi1)
      rlNormal3f(st1 * cp1, ct1, st1 * sp1);
      rlTexCoord2f(u1, vt1);
      rlVertex3f(st1 * cp1 * r1, ct1 * r1, st1 * sp1 * r1);

      // Đỉnh 2 (theta1, phi2) - Vị trí hình học vẫn dùng cp2, sp2 để khép vòng
      rlNormal3f(st1 * cp2, ct1, st1 * sp2);
      rlTexCoord2f(u2, vt1);
      rlVertex3f(st1 * cp2 * r2, ct1 * r2, st1 * sp2 * r2);

      // Đỉnh 3 (theta2, phi2)
      rlNormal3f(st2 * cp2, ct2, st2 * sp2);
      rlTexCoord2f(u2, vt2);
      rlVertex3f(st2 * cp2 * r3, ct2 * r3, st2 * sp2 * r3);

      // Đỉnh 4 (theta2, phi1)
      rlNormal3f(st2 * cp1, ct2, st2 * sp1);
      rlTexCoord2f(u1, vt2);
      rlVertex3f(st2 * cp1 * r4, ct2 * r4, st2 * sp1 * r4);
    }
  }

  rlEnd();
  rlPopMatrix();
}
void InitFluidSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  fluidShader = LoadShader("fluid.vs", "fluid.fs");

  // u_time là uniform dùng chung cho VS và FS — một location, một lần
  // SetShaderValue
  timeLoc = GetShaderLocation(fluidShader, "u_time");
  viewPosLoc = GetShaderLocation(fluidShader, "viewPos");

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;
}

void CastFluidSkill(Vector3 startPos, Vector3 target, float twistPhase,
                    float sizeScale) {
  (void)twistPhase;
  float clampedScale = ClampSizeScale(sizeScale);

  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].progress = 0.0f;
      emitters[i].spawnAccumulator = 0.0f;
      emitters[i].sizeScale = clampedScale;
      emitters[i].currentPos = startPos;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      float arcHeight = dist * 0.25f;

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f));
      emitters[i].p1.y += arcHeight;
      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.65f));
      emitters[i].p2.y += arcHeight;
      break;
    }
  }

  int burstCount = GetRandomValue(10, 16) * clampedScale;
  for (int s = 0; s < burstCount; s++) {
    ParticleConfig cfg = {0};
    cfg.position = startPos;
    cfg.velocity =
        (Vector3){Math_Mix(-150.0f, 250.0f, Random01()) * clampedScale,
                  Math_Mix(0.0f, 300.0f, Random01()) * clampedScale,
                  Math_Mix(-150.0f, 250.0f, Random01()) * clampedScale};
    cfg.force = (Vector3){0.0f, GRAVITY_Y, 0.0f};
    cfg.drag = FLUID_DRAG_SPLASH;
    cfg.radius = Math_Mix(3.0f, 8.0f, Random01()) * clampedScale * 3.5f;
    cfg.lifetime = Math_Mix(0.3f, 0.8f, Random01());
    cfg.colorStart = (Color){190, 235, 255, 220};
    cfg.colorEnd = (Color){40, 120, 200, 0};
    cfg.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
    SpawnParticle(cfg);
  }
}

void UpdateFluidSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    emitters[e].progress += dt * WATER_TRAVEL_SPEED;

    if (emitters[e].progress >= 1.0f) {
      emitters[e].active = false;
      TriggerFluidImpact(emitters[e].targetPos, emitters[e].sizeScale);
      continue;
    }

    emitters[e].currentPos =
        GetBezierPoint(emitters[e].startPos, emitters[e].p1, emitters[e].p2,
                       emitters[e].targetPos, emitters[e].progress);

    emitters[e].spawnAccumulator +=
        MIST_SPAWN_RATE * emitters[e].sizeScale * dt;
    int dropToSpawn = (int)emitters[e].spawnAccumulator;
    emitters[e].spawnAccumulator -= dropToSpawn;

    // for (int k = 0; k < dropToSpawn; k++) {
    //   Vector3 randomDir = Vector3Normalize(
    //       (Vector3){Random01() - 0.5f, Random01() - 0.5f, Random01() -
    //       0.5f});
    //   ParticleConfig cfgMist = {0};
    //   cfgMist.position =
    //       Vector3Add(emitters[e].currentPos,
    //                  Vector3Scale(randomDir, 6.0f * emitters[e].sizeScale));
    //   cfgMist.velocity =
    //       Vector3Scale(randomDir, Math_Mix(20.0f, 60.0f, Random01()) *
    //                                   emitters[e].sizeScale);
    //   cfgMist.force = (Vector3){0.0f, GRAVITY_Y * 0.8f, 0.0f};
    //   cfgMist.drag = FLUID_DRAG_MIST;
    //   cfgMist.radius =
    //       Math_Mix(1.5f, 4.0f, Random01()) * emitters[e].sizeScale * 2.5f;
    //   cfgMist.lifetime = Math_Mix(0.3f, 0.6f, Random01());
    //   cfgMist.colorStart = (Color){200, 245, 255, 180};
    //   cfgMist.colorEnd = (Color){100, 150, 200, 0};
    //   cfgMist.physicsFlags = P_PHYSICS_DRAG | P_PHYSICS_FORCE;
    //   SpawnParticle(cfgMist);
    // }
  }
}

void DrawFluidSkill(void) {
  // Early exit để tránh setup shader/blend khi không có gì bay
  bool anyActive = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      anyActive = true;
      break;
    }
  }
  if (!anyActive)
    return;

  float time = GetTime();

  rlDisableDepthMask();
  rlEnableBackfaceCulling();
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(fluidShader);

  // u_time dùng chung VS + FS — chỉ cần set một lần
  SetShaderValue(fluidShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(fluidShader, viewPosLoc, &camera.position,
                 SHADER_UNIFORM_VEC3);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;
    float radius = WATER_BASE_RADIUS * emitters[e].sizeScale;
    RenderCustom3DSphere(emitters[e].currentPos, radius, 0.65f, time);
  }

  EndShaderMode();
  EndBlendMode();
  rlDisableBackfaceCulling();
  rlEnableDepthMask();
}

void UnloadFluidSkill(void) { UnloadShader(fluidShader); }

int GetFluidSkillProjectiles(SkillProjectile *outProjectiles,
                             int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      outProjectiles[count].position = emitters[i].currentPos;
      outProjectiles[count].radius = WATER_BASE_RADIUS * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateFluidProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        TriggerFluidImpact(emitters[i].currentPos, emitters[i].sizeScale);
        return; // early return thay vì break + fall-through vô nghĩa
      }
      count++;
    }
  }
}