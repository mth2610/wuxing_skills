#include "core/particle_system.h"
#include "core/force_field.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/utils_math.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define MAX_GLOBAL_PARTICLES 4000
#else
#define MAX_GLOBAL_PARTICLES 20000
#endif

typedef struct {
  Vector4 pos_radius;
  Vector4 vel_drag;
  // GPU mode (xem dưới): x = forceFieldSlot (float, -1 = không dùng), yzw đệm.
  // CPU mode (__APPLE__) KHÔNG dùng field này — particle giữ con trỏ
  // ForceField riêng trong cpuForceFields[], field này luôn = -1/0 cho an toàn.
  Vector4 force_turb;
  Vector4 colorStart;
  Vector4 colorEnd;
  Vector4 lifeData; // x: lifetime còn lại, y: tổng lifetime, z: phase nhiễu, w:
                    // dự trữ (không còn dùng cho viscosity)
} ParticleGPU;

static int lastUsedIndex = 0;

// Build phần dữ liệu CHUNG cho CPU/GPU từ ParticleConfig (tránh duplicate
// logic màu/lifetime giữa 2 nhánh #ifdef — trước đây 2 bên copy gần như
// y nguyên nhau, dễ desync khi sửa 1 bên mà quên bên kia).
static ParticleGPU BuildParticleGPUCommon(ParticleConfig config) {
  ParticleGPU p = {0};

  p.pos_radius = (Vector4){config.position.x, config.position.y,
                           config.position.z, config.radius};
  p.vel_drag = (Vector4){config.velocity.x, config.velocity.y,
                         config.velocity.z, 0.0f}; // drag đã được chuyển hoàn toàn vào ForceField
  p.force_turb =
      (Vector4){-1.0f, 0.0f, 0.0f, 0.0f}; // -1 = chưa gán force field slot
  p.colorStart =
      (Vector4){config.colorStart.r / 255.0f, config.colorStart.g / 255.0f,
                config.colorStart.b / 255.0f, config.colorStart.a / 255.0f};
  p.colorEnd =
      (Vector4){config.colorEnd.r / 255.0f, config.colorEnd.g / 255.0f,
                config.colorEnd.b / 255.0f, config.colorEnd.a / 255.0f};
  p.lifeData =
      (Vector4){config.lifetime, config.lifetime, Random01() * PI * 2.0f, 0.0f};

  return p;
}

// =============================================================================
// macOS: CPU MODE (SIÊU TỐI ƯU CƠ CHẾ DENSE ARRAY)
// =============================================================================
#ifdef __APPLE__

static ParticleGPU *cpuParticles = NULL;
static const ForceField **cpuForceFields =
    NULL; // mảng con trỏ ForceField song song
static int activeParticleCount = 0;
static Shader cpuRenderShader;
static Mesh cpuMesh;
static Material cpuMaterial;

void InitParticleSystem(void) {
  cpuRenderShader = LoadShader("core/shaders/particles_cpu.vs", "core/shaders/particles_cpu.fs");
  cpuParticles =
      (ParticleGPU *)calloc(MAX_GLOBAL_PARTICLES, sizeof(ParticleGPU));
  cpuForceFields =
      (const ForceField **)calloc(MAX_GLOBAL_PARTICLES, sizeof(ForceField *));
  activeParticleCount = 0;

  int totalVertices = MAX_GLOBAL_PARTICLES * 4;
  int totalTriangles = MAX_GLOBAL_PARTICLES * 2;
  int totalIndices = MAX_GLOBAL_PARTICLES * 6;

  cpuMesh.vertexCount = totalVertices;
  cpuMesh.triangleCount = totalTriangles;
  cpuMesh.vertices = (float *)calloc(totalVertices * 3, sizeof(float));
  cpuMesh.texcoords = (float *)calloc(totalVertices * 2, sizeof(float));
  cpuMesh.colors =
      (unsigned char *)calloc(totalVertices * 4, sizeof(unsigned char));
  cpuMesh.indices =
      (unsigned short *)calloc(totalIndices, sizeof(unsigned short));

  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    int vi = i * 4;
    int ii = i * 6;
    cpuMesh.indices[ii + 0] = (unsigned short)(vi + 0);
    cpuMesh.indices[ii + 1] = (unsigned short)(vi + 1);
    cpuMesh.indices[ii + 2] = (unsigned short)(vi + 2);
    cpuMesh.indices[ii + 3] = (unsigned short)(vi + 0);
    cpuMesh.indices[ii + 4] = (unsigned short)(vi + 2);
    cpuMesh.indices[ii + 5] = (unsigned short)(vi + 3);
  }

  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    int vi = i * 4;
    cpuMesh.texcoords[(vi + 0) * 2 + 0] = 0.0f;
    cpuMesh.texcoords[(vi + 0) * 2 + 1] = 1.0f;
    cpuMesh.texcoords[(vi + 1) * 2 + 0] = 1.0f;
    cpuMesh.texcoords[(vi + 1) * 2 + 1] = 1.0f;
    cpuMesh.texcoords[(vi + 2) * 2 + 0] = 1.0f;
    cpuMesh.texcoords[(vi + 2) * 2 + 1] = 0.0f;
    cpuMesh.texcoords[(vi + 3) * 2 + 0] = 0.0f;
    cpuMesh.texcoords[(vi + 3) * 2 + 1] = 0.0f;
  }

  UploadMesh(&cpuMesh, true);
  cpuMaterial = LoadMaterialDefault();
  cpuMaterial.shader = cpuRenderShader;
  lastUsedIndex = 0;
}

void SpawnParticle(ParticleConfig config) {
  if (activeParticleCount >= MAX_GLOBAL_PARTICLES)
    return;

  cpuParticles[activeParticleCount] = BuildParticleGPUCommon(config);

  // Lưu con trỏ ForceField của particle này (NULL nếu không dùng)
  cpuForceFields[activeParticleCount] = config.forceField;

  activeParticleCount++;
}

void UpdateParticles(float dt) {
  if (activeParticleCount == 0)
    return;

  float time = (float)GetTime();

  for (int i = activeParticleCount - 1; i >= 0; i--) {
    ParticleGPU *p = &cpuParticles[i];

    p->lifeData.x -= dt;
    if (p->lifeData.x <= 0.0f) {
      // Dense-array compact: ghi đè bằng phần tử cuối, hoán đổi cả FF pointer
      cpuParticles[i] = cpuParticles[activeParticleCount - 1];
      cpuForceFields[i] = cpuForceFields[activeParticleCount - 1];
      activeParticleCount--;
      continue;
    }

    float velX = p->vel_drag.x;
    float velY = p->vel_drag.y;
    float velZ = p->vel_drag.z;

    // ÁP DỤNG FORCE FIELD (nếu particle này có gán ForceField)
    if (cpuForceFields[i]) {
      Vector3 curPos = {p->pos_radius.x, p->pos_radius.y, p->pos_radius.z};
      Vector3 curVel = {velX, velY, velZ};
      // Acceleration từ gravity / noise / vortex / ...
      Vector3 acc = ForceField_Evaluate(cpuForceFields[i], curPos, curVel, time,
                                        (Vector3){0}, (Vector3){0});
      velX += acc.x * dt;
      velY += acc.y * dt;
      velZ += acc.z * dt;
      // Viscosity damping từ FORCE_VISCOSITY layer (multiplicative, sau
      // acceleration)
      float damp = ForceField_GetViscosityDamping(cpuForceFields[i], dt);
      velX *= damp;
      velY *= damp;
      velZ *= damp;
    }

    p->vel_drag.x = velX;
    p->vel_drag.y = velY;
    p->vel_drag.z = velZ;
    p->pos_radius.x += velX * dt;
    p->pos_radius.y += velY * dt;
    p->pos_radius.z += velZ * dt;
  }
}

static void BuildParticleMeshData(Camera3D camera) {
  if (activeParticleCount == 0)
    return;

  Matrix matView = GetCameraMatrix(camera);
  Vector3 camRight = {matView.m0, matView.m4, matView.m8};
  Vector3 camUp = {matView.m1, matView.m5, matView.m9};

  for (int i = 0; i < activeParticleCount; i++) {
    ParticleGPU *p = &cpuParticles[i];
    int vi = i * 4;

    float lifeRatio = p->lifeData.x / p->lifeData.y;
    float r = p->colorEnd.x + (p->colorStart.x - p->colorEnd.x) * lifeRatio;
    float g = p->colorEnd.y + (p->colorStart.y - p->colorEnd.y) * lifeRatio;
    float b = p->colorEnd.z + (p->colorStart.z - p->colorEnd.z) * lifeRatio;
    float a = p->colorEnd.w +
              (p->colorStart.w - p->colorEnd.w) * lifeRatio * lifeRatio;

    unsigned char cr = (unsigned char)(r * 255.0f);
    unsigned char cg = (unsigned char)(g * 255.0f);
    unsigned char cb = (unsigned char)(b * 255.0f);
    unsigned char ca = (unsigned char)(a * 255.0f);

    float radius = p->pos_radius.w;
    float px = p->pos_radius.x;
    float py = p->pos_radius.y;
    float pz = p->pos_radius.z;

    float rxR = camRight.x * radius;
    float ryR = camRight.y * radius;
    float rzR = camRight.z * radius;
    float rxU = camUp.x * radius;
    float ryU = camUp.y * radius;
    float rzU = camUp.z * radius;

    cpuMesh.vertices[(vi + 0) * 3 + 0] = px - rxR - rxU;
    cpuMesh.vertices[(vi + 0) * 3 + 1] = py - ryR - ryU;
    cpuMesh.vertices[(vi + 0) * 3 + 2] = pz - rzR - rzU;

    cpuMesh.vertices[(vi + 1) * 3 + 0] = px + rxR - rxU;
    cpuMesh.vertices[(vi + 1) * 3 + 1] = py + ryR - ryU;
    cpuMesh.vertices[(vi + 1) * 3 + 2] = pz + rzR - rzU;

    cpuMesh.vertices[(vi + 2) * 3 + 0] = px + rxR + rxU;
    cpuMesh.vertices[(vi + 2) * 3 + 1] = py + ryR + ryU;
    cpuMesh.vertices[(vi + 2) * 3 + 2] = pz + rzR + rzU;

    cpuMesh.vertices[(vi + 3) * 3 + 0] = px - rxR + rxU;
    cpuMesh.vertices[(vi + 3) * 3 + 1] = py - ryR + ryU;
    cpuMesh.vertices[(vi + 3) * 3 + 2] = pz - rzR + rzU;

    for (int v = 0; v < 4; v++) {
      cpuMesh.colors[(vi + v) * 4 + 0] = cr;
      cpuMesh.colors[(vi + v) * 4 + 1] = cg;
      cpuMesh.colors[(vi + v) * 4 + 2] = cb;
      cpuMesh.colors[(vi + v) * 4 + 3] = ca;
    }
  }

  UpdateMeshBuffer(cpuMesh, 0, cpuMesh.vertices,
                   activeParticleCount * 4 * 3 * sizeof(float), 0);
  UpdateMeshBuffer(cpuMesh, 3, cpuMesh.colors,
                   activeParticleCount * 4 * 4 * sizeof(unsigned char), 0);
}

void DrawParticles(Camera3D camera, Texture2D texture) {
  if (activeParticleCount == 0)
    return;

  BuildParticleMeshData(camera);

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  cpuMaterial.maps[MATERIAL_MAP_ALBEDO].texture = texture;

  cpuMesh.vertexCount = activeParticleCount * 4;
  cpuMesh.triangleCount = activeParticleCount * 2;

  DrawMesh(cpuMesh, cpuMaterial, MatrixIdentity());

  EndBlendMode();
  rlEnableDepthMask();
}

bool IsParticleSystemActive(void) { return activeParticleCount > 0; }

void UnloadParticleSystem(void) {
  UnloadShader(cpuRenderShader);
  UnloadMesh(cpuMesh);
  free(cpuParticles);
  free(cpuForceFields);
  cpuParticles = NULL;
  cpuForceFields = NULL;
  activeParticleCount = 0;
}

// CPU mode không có registry/slot — particle giữ trực tiếp con trỏ ForceField,
// không có giới hạn MAX_GPU_FORCE_FIELDS nào để mà reset.
void ParticleSystem_ResetForceFieldRegistry(void) {
  // no-op trên CPU mode
}

// =============================================================================
// Linux/Windows/Android: GPU COMPUTE SHADER MODE
// =============================================================================
#else

// Local size của compute shader (PHẢI khớp `local_size_x` trong
// particles.comp). Dùng 128 thay vì 256: 128 là giá trị
// MAX_COMPUTE_WORK_GROUP_INVOCATIONS tối thiểu được OpenGL ES 3.1 BẢO ĐẢM hỗ
// trợ trên mọi thiết bị compute-capable. 256 có thể fail link/dispatch trên một
// số GPU Android tầm trung/thấp.
#define COMPUTE_LOCAL_SIZE 128

// Số ForceField khác nhau đang active trên GPU, song song với
// registeredFields[]
#define _MAX_GPU_FF MAX_GPU_FORCE_FIELDS

static unsigned int ssboParticleId = 0;
static unsigned int ssboForceFieldId = 0;
static unsigned int computeProgramId = 0;
static int computeDtLoc;
static int computeTimeLoc;
static int computeCountLoc;

static Shader renderShader;
static Mesh quadMesh;
static Material particleMaterial;
static Matrix *dummyInstancedTransforms;

// Tổng số particle đã từng spawn, bão hòa ở MAX_GLOBAL_PARTICLES sau khi
// ring buffer wrap lần đầu. Dùng để KHÔNG dispatch/draw toàn bộ
// MAX_GLOBAL_PARTICLES mỗi frame khi thực tế chỉ có vài chục hạt đang sống —
// quan trọng cho mobile vì DrawMeshInstanced/dispatch trước đây luôn full
// budget (20000 quad mỗi frame) bất kể có bao nhiêu hạt thật sự "sống".
static int totalSpawned = 0;

// Registry ForceField đang được particle GPU tham chiếu tới (qua con trỏ),
// dùng để biết slot nào ứng với ForceField nào khi serialize lên SSBO thứ 2.
static const ForceField *registeredFields[MAX_GPU_FORCE_FIELDS];
static int registeredFieldCount = 0;

// Tìm hoặc đăng ký slot GPU cho 1 ForceField. Trả -1 nếu ff NULL hoặc hết slot
// (particle vẫn spawn bình thường, chỉ là sẽ không có force field trên GPU).
static int RegisterForceFieldGPU(const ForceField *ff) {
  if (!ff)
    return -1;

  for (int i = 0; i < registeredFieldCount; i++) {
    if (registeredFields[i] == ff)
      return i;
  }

  if (registeredFieldCount >= MAX_GPU_FORCE_FIELDS) {
    TraceLog(LOG_WARNING,
             "ParticleSystem: vuot qua MAX_GPU_FORCE_FIELDS (%d) - particle "
             "nay se khong co force field tren GPU. Goi "
             "ParticleSystem_ResetForceFieldRegistry() giua cac man/round neu "
             "can giai phong slot cu.",
             MAX_GPU_FORCE_FIELDS);
    return -1;
  }

  int slot = registeredFieldCount++;
  registeredFields[slot] = ff;
  return slot;
}

void ParticleSystem_ResetForceFieldRegistry(void) {
  registeredFieldCount = 0;
  memset(registeredFields, 0, sizeof(registeredFields));
}

// particles.comp được viết với "#version 430 core" làm placeholder để mở
// trực tiếp bằng editor/desktop preview cho dễ — nhưng GLSL ES (Android)
// KHÔNG hiểu cú pháp "core" và compute shader yêu cầu tối thiểu
// "#version 310 es". Hàm này bóc dòng #version gốc ra rồi chèn lại version +
// precision đúng theo platform thực tế tại runtime (rlGetVersion()), để 1 file
// .comp duy nhất chạy được trên cả desktop GL và GLES Android.
//
// LƯU Ý: tên enum OPENGL_ES_20/OPENGL_ES_30 có thể có tiền tố RL_ tuỳ phiên
// bản raylib (rlgl.h). Macro bên dưới tự chọn theo cái nào tồn tại — nhưng
// nên double-check lại với rlgl.h đang dùng nếu compile lỗi ở đây.
#if defined(RL_OPENGL_ES_20)
#define PS_GL_ES_20 RL_OPENGL_ES_20
#define PS_GL_ES_30 RL_OPENGL_ES_30
#else
#define PS_GL_ES_20 OPENGL_ES_20
#define PS_GL_ES_30 OPENGL_ES_30
#endif

static char *PreprocessComputeShaderVersion(const char *src) {
  const char *body = src;
  if (strncmp(src, "#version", 8) == 0) {
    const char *nl = strchr(src, '\n');
    if (nl)
      body = nl + 1;
  }

  int glVersion = rlGetVersion();
  bool isES = (glVersion == PS_GL_ES_20) || (glVersion == PS_GL_ES_30);

  const char *header =
      isES ? "#version 310 es\nprecision highp float;\nprecision highp int;\n"
           : "#version 430 core\n";

  size_t len = strlen(header) + strlen(body) + 1;
  char *out = (char *)malloc(len);
  if (out)
    snprintf(out, len, "%s%s", header, body);
  return out;
}

void InitParticleSystem(void) {
  renderShader = LoadShader("core/shaders/particles.vs", "core/shaders/particles.fs");

  char *rawCode = LoadFileText("core/shaders/particles.comp");
  char *computeCode = PreprocessComputeShaderVersion(rawCode);
  UnloadFileText(rawCode);

  unsigned int computeShaderId =
      rlCompileShader(computeCode, RL_COMPUTE_SHADER);
  computeProgramId = rlLoadComputeShaderProgram(computeShaderId);
  free(computeCode);

  computeDtLoc = rlGetLocationUniform(computeProgramId, "u_dt");
  computeTimeLoc = rlGetLocationUniform(computeProgramId, "u_time");
  computeCountLoc = rlGetLocationUniform(computeProgramId, "u_particleCount");

  int bufferSize = MAX_GLOBAL_PARTICLES * sizeof(ParticleGPU);
  ssboParticleId = rlLoadShaderBuffer(bufferSize, NULL, RL_DYNAMIC_COPY);

  ParticleGPU *emptyData = calloc(MAX_GLOBAL_PARTICLES, sizeof(ParticleGPU));
  rlUpdateShaderBuffer(ssboParticleId, emptyData, bufferSize, 0);
  free(emptyData);

  // SSBO thứ 2: layer của các ForceField đang active, binding = 1 trong compute
  // shader
  int ffBufferSize = MAX_GPU_FORCE_FIELDS * sizeof(ForceFieldGPU);
  ssboForceFieldId = rlLoadShaderBuffer(ffBufferSize, NULL, RL_DYNAMIC_COPY);
  ForceFieldGPU *emptyFF = calloc(MAX_GPU_FORCE_FIELDS, sizeof(ForceFieldGPU));
  rlUpdateShaderBuffer(ssboForceFieldId, emptyFF, ffBufferSize, 0);
  free(emptyFF);

  quadMesh = GenMeshPlane(2.0f, 2.0f, 1, 1);

  dummyInstancedTransforms = calloc(MAX_GLOBAL_PARTICLES, sizeof(Matrix));
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    dummyInstancedTransforms[i] = MatrixIdentity();
  }

  particleMaterial = LoadMaterialDefault();
  particleMaterial.shader = renderShader;
  lastUsedIndex = 0;
  totalSpawned = 0;
  ParticleSystem_ResetForceFieldRegistry();
}

void SpawnParticle(ParticleConfig config) {
  ParticleGPU gpuPart = BuildParticleGPUCommon(config);
  gpuPart.force_turb.x = (float)RegisterForceFieldGPU(config.forceField);

  int offset = lastUsedIndex * sizeof(ParticleGPU);
  rlUpdateShaderBuffer(ssboParticleId, &gpuPart, sizeof(ParticleGPU), offset);

  // RING BUFFER: ghi đè slot tiếp theo bất kể hạt ở đó còn sống hay không.
  // Nếu spawn rate > tốc độ chết tự nhiên (nhiều skill VFX chồng lên nhau),
  // có thể "giết" sớm một hạt đang sống -> pop nhẹ. Đổi lại là O(1) spawn,
  // không cần GPU readback để biết slot nào đang free. Nếu sau này thấy pop
  // rõ ràng trong combat nhiều hiệu ứng, đây là nơi cần nâng cấp đầu tiên.
  lastUsedIndex = (lastUsedIndex + 1) % MAX_GLOBAL_PARTICLES;
  if (totalSpawned < MAX_GLOBAL_PARTICLES)
    totalSpawned++;
}

void UpdateParticles(float dt) {
  if (totalSpawned == 0)
    return; // chưa từng spawn gì, khỏi cần re-upload/dispatch

  // Re-pack & re-upload TẤT CẢ ForceField đang đăng ký mỗi frame — vì nội
  // dung layer (strength gió, vortex, v.v.) có thể đổi runtime, và compute
  // shader cần thấy giá trị mới nhất, giống cách CPU path luôn đọc trực tiếp
  // từ con trỏ ForceField mỗi frame.
  if (registeredFieldCount > 0) {
    static ForceFieldGPU packed[MAX_GPU_FORCE_FIELDS];
    for (int i = 0; i < registeredFieldCount; i++) {
      ForceField_PackGPU(registeredFields[i], &packed[i]);
    }
    rlUpdateShaderBuffer(ssboForceFieldId, packed,
                         registeredFieldCount * sizeof(ForceFieldGPU), 0);
  }

  float time = (float)GetTime();

  rlEnableShader(computeProgramId);
  rlSetUniform(computeDtLoc, &dt, SHADER_UNIFORM_FLOAT, 1);
  rlSetUniform(computeTimeLoc, &time, SHADER_UNIFORM_FLOAT, 1);
  rlSetUniform(computeCountLoc, &totalSpawned, SHADER_UNIFORM_INT, 1);
  rlBindShaderBuffer(ssboParticleId, 0);
  rlBindShaderBuffer(ssboForceFieldId, 1);

  // Chỉ dispatch đủ workgroup cho số particle ĐÃ TỪNG spawn, không phải toàn
  // bộ MAX_GLOBAL_PARTICLES — tiết kiệm đáng kể compute trên mobile khi scene
  // chỉ có vài chục/trăm hạt thay vì full 20000 budget.
  int workGroupsX =
      (totalSpawned + COMPUTE_LOCAL_SIZE - 1) / COMPUTE_LOCAL_SIZE;
  if (workGroupsX < 1)
    workGroupsX = 1;
  rlComputeShaderDispatch(workGroupsX, 1, 1);
  rlDisableShader();
}

void DrawParticles(Camera3D camera, Texture2D texture) {
  if (totalSpawned == 0)
    return;

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  particleMaterial.maps[MATERIAL_MAP_ALBEDO].texture = texture;

  Matrix matView = GetCameraMatrix(camera);
  int matViewLoc = GetShaderLocation(renderShader, "matView");
  SetShaderValueMatrix(renderShader, matViewLoc, matView);

  rlBindShaderBuffer(ssboParticleId, 0);
  // Vẽ đúng totalSpawned instance thay vì luôn full MAX_GLOBAL_PARTICLES —
  // hạt "chết" (lifeData.x <= 0) bên trong range này vẫn được vertex/fragment
  // shader xử lý (nên particles.vs/fs cần tự thu nhỏ/discard hạt chết), nhưng
  // ít nhất ta không transform/issue cả 20000 quad khi chỉ có vài chục hạt.
  DrawMeshInstanced(quadMesh, particleMaterial, dummyInstancedTransforms,
                    totalSpawned);

  EndBlendMode();
  rlEnableDepthMask();
}

// Vẫn chỉ là xấp xỉ: true nếu đã từng spawn ít nhất 1 hạt. Không biết chính
// xác có hạt nào ĐANG sống hay không vì không readback GPU buffer (tốn hiệu
// năng, gây stall). Đủ dùng cho mục đích "có cần update/draw hệ thống này
// không" ở cấp game loop.
bool IsParticleSystemActive(void) { return totalSpawned > 0; }

void UnloadParticleSystem(void) {
  UnloadShader(renderShader);
  rlUnloadShaderProgram(computeProgramId);
  rlUnloadShaderBuffer(ssboParticleId);
  rlUnloadShaderBuffer(ssboForceFieldId);
  UnloadMesh(quadMesh);
  free(dummyInstancedTransforms);

  lastUsedIndex = 0;
  totalSpawned = 0;
  ParticleSystem_ResetForceFieldRegistry();
}

#endif // __APPLE__