#include "particle_system.h"
#include "raymath.h"
#include "rlgl.h"
#include "utils_math.h"
#include <math.h>
#include <stdlib.h>

#ifdef __APPLE__
#define MAX_GLOBAL_PARTICLES 4000
#else
#define MAX_GLOBAL_PARTICLES 20000
#endif

typedef struct {
  Vector4 pos_radius;
  Vector4 vel_drag;
  Vector4 force_turb;
  Vector4 colorStart;
  Vector4 colorEnd;
  Vector4 lifeData;
} ParticleGPU;

static int lastUsedIndex = 0;

// =============================================================================
// macOS: CPU MODE (SIÊU TỐI ƯU CƠ CHẾ DENSE ARRAY)
// =============================================================================
#ifdef __APPLE__

static ParticleGPU *cpuParticles = NULL;
static int activeParticleCount = 0; // TỐI ƯU: Quản lý mật độ hạt sống
static Shader cpuRenderShader;
static Mesh cpuMesh;
static Material cpuMaterial;

void InitParticleSystem(void) {
  cpuRenderShader = LoadShader("particles_cpu.vs", "particles_cpu.fs");
  cpuParticles =
      (ParticleGPU *)calloc(MAX_GLOBAL_PARTICLES, sizeof(ParticleGPU));
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

  // Cấu hình Indices cố định 1 lần duy nhất
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

  // Cấu hình UV cố định 1 lần duy nhất
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
  // Nếu mảng đầy, không sinh nữa để bảo vệ bộ nhớ
  if (activeParticleCount >= MAX_GLOBAL_PARTICLES)
    return;

  // Luôn sinh hạt mới ở cuối phân vùng active (Cực nhanh, tốn O(1))
  ParticleGPU *p = &cpuParticles[activeParticleCount];

  p->pos_radius = (Vector4){config.position.x, config.position.y,
                            config.position.z, config.radius};
  p->vel_drag = (Vector4){config.velocity.x, config.velocity.y,
                          config.velocity.z, config.drag};
  p->force_turb = (Vector4){config.force.x, config.force.y, config.force.z,
                            config.turbulence};
  p->colorStart =
      (Vector4){config.colorStart.r / 255.0f, config.colorStart.g / 255.0f,
                config.colorStart.b / 255.0f, config.colorStart.a / 255.0f};
  p->colorEnd =
      (Vector4){config.colorEnd.r / 255.0f, config.colorEnd.g / 255.0f,
                config.colorEnd.b / 255.0f, config.colorEnd.a / 255.0f};
  p->lifeData =
      (Vector4){config.lifetime, config.lifetime, Random01() * PI * 2.0f, 1.0f};

  activeParticleCount++;
}

void UpdateParticles(float dt) {
  if (activeParticleCount == 0)
    return;

  float time = (float)GetTime();

  // TỐI ƯU CHÌA KHÓA: Duyệt ngược từ đuôi vùng sống về đầu để Swap-and-Pop an
  // toàn
  for (int i = activeParticleCount - 1; i >= 0; i--) {
    ParticleGPU *p = &cpuParticles[i];

    p->lifeData.x -= dt;
    if (p->lifeData.x <= 0.0f) {
      // SWAP-AND-POP: Bốc hạt cuối cùng đang sống trám vào ô của hạt vừa chết
      cpuParticles[i] = cpuParticles[activeParticleCount - 1];
      activeParticleCount--;
      continue;
    }

    float lifeRatio = p->lifeData.x / p->lifeData.y;

    // Cache các biến ra thanh ghi (Registers) tránh truy xuất mảng liên tục
    float velX = p->vel_drag.x;
    float velY = p->vel_drag.y;
    float velZ = p->vel_drag.z;
    float drag = p->vel_drag.w;
    float forceX = p->force_turb.x;
    float forceY = p->force_turb.y;
    float forceZ = p->force_turb.z;
    float turb = p->force_turb.w;

    if (drag > 0.0f) {
      float factor = 1.0f - drag * dt;
      velX *= factor;
      velY *= factor;
      velZ *= factor;
    }

    velX += forceX * dt;
    velY += forceY * dt;
    velZ += forceZ * dt;

    if (turb > 0.0f) {
      float t = time * 18.0f + p->lifeData.z;
      float turbStrength = turb * lifeRatio;
      float sinT = sinf(t);
      float cosT = cosf(t * 0.8f);
      velX += sinT * turbStrength * dt;
      velY += cosT * turbStrength * dt;
      velZ += (sinT * cosT) * turbStrength * dt;
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

  // TỐI ƯU: Chỉ xử lý và tính toán toán học cho các hạt ĐANG SỐNG
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

    // Trích xuất góc hình vuông toán học thẳng về camera
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

  // TỐI ƯU SIÊU TỐC: Chỉ nạp đúng số lượng Byte của hạt sống lên VRAM, bỏ mặc
  // vùng đuôi thừa
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

  // TỐI ƯU: Điều chỉnh số lượng tam giác vẽ thực tế dựa trên số hạt sống
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
  cpuParticles = NULL;
  activeParticleCount = 0;
}

// =============================================================================
// Linux/Windows/Android: GPU COMPUTE SHADER MODE (GIỮ NGUYÊN KIẾN TRÚC GỐC)
// =============================================================================
#else

static unsigned int ssboParticleId = 0;
static unsigned int computeProgramId = 0;
static int computeDtLoc;
static int computeTimeLoc;

static Shader renderShader;
static Mesh quadMesh;
static Material particleMaterial;
static Matrix *dummyInstancedTransforms;

void InitParticleSystem(void) {
  renderShader = LoadShader("particles.vs", "particles.fs");

  char *computeCode = LoadFileText("particles.comp");
  unsigned int computeShaderId =
      rlCompileShader(computeCode, RL_COMPUTE_SHADER);
  computeProgramId = rlLoadComputeShaderProgram(computeShaderId);
  UnloadFileText(computeCode);

  computeDtLoc = rlGetLocationUniform(computeProgramId, "u_dt");
  computeTimeLoc = rlGetLocationUniform(computeProgramId, "u_time");

  int bufferSize = MAX_GLOBAL_PARTICLES * sizeof(ParticleGPU);
  ssboParticleId = rlLoadShaderBuffer(bufferSize, NULL, RL_DYNAMIC_COPY);

  ParticleGPU *emptyData = calloc(MAX_GLOBAL_PARTICLES, sizeof(ParticleGPU));
  rlUpdateShaderBuffer(ssboParticleId, emptyData, bufferSize, 0);
  free(emptyData);

  quadMesh = GenMeshPlane(2.0f, 2.0f, 1, 1);

  dummyInstancedTransforms = calloc(MAX_GLOBAL_PARTICLES, sizeof(Matrix));
  for (int i = 0; i < MAX_GLOBAL_PARTICLES; i++) {
    dummyInstancedTransforms[i] = MatrixIdentity();
  }

  particleMaterial = LoadMaterialDefault();
  particleMaterial.shader = renderShader;
  lastUsedIndex = 0;
}

void SpawnParticle(ParticleConfig config) {
  ParticleGPU gpuPart = {0};

  gpuPart.pos_radius = (Vector4){config.position.x, config.position.y,
                                 config.position.z, config.radius};
  gpuPart.vel_drag = (Vector4){config.velocity.x, config.velocity.y,
                               config.velocity.z, config.drag};
  gpuPart.force_turb = (Vector4){config.force.x, config.force.y, config.force.z,
                                 config.turbulence};
  gpuPart.colorStart =
      (Vector4){config.colorStart.r / 255.0f, config.colorStart.g / 255.0f,
                config.colorStart.b / 255.0f, config.colorStart.a / 255.0f};
  gpuPart.colorEnd =
      (Vector4){config.colorEnd.r / 255.0f, config.colorEnd.g / 255.0f,
                config.colorEnd.b / 255.0f, config.colorEnd.a / 255.0f};
  gpuPart.lifeData =
      (Vector4){config.lifetime, config.lifetime, Random01() * PI * 2.0f, 1.0f};

  int offset = lastUsedIndex * sizeof(ParticleGPU);
  rlUpdateShaderBuffer(ssboParticleId, &gpuPart, sizeof(ParticleGPU), offset);

  lastUsedIndex = (lastUsedIndex + 1) % MAX_GLOBAL_PARTICLES;
}

void UpdateParticles(float dt) {
  rlEnableShader(computeProgramId);
  rlSetUniform(computeDtLoc, &dt, SHADER_UNIFORM_FLOAT, 1);
  rlSetUniform(computeTimeLoc, &(float){(float)GetTime()}, SHADER_UNIFORM_FLOAT,
               1);
  rlBindShaderBuffer(ssboParticleId, 0);

  int workGroupsX = (MAX_GLOBAL_PARTICLES / 256) + 1;
  rlComputeShaderDispatch(workGroupsX, 1, 1);
  rlDisableShader();
}

void DrawParticles(Camera3D camera, Texture2D texture) {
  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  particleMaterial.maps[MATERIAL_MAP_ALBEDO].texture = texture;

  Matrix matView = GetCameraMatrix(camera);
  int matViewLoc = GetShaderLocation(renderShader, "matView");
  SetShaderValueMatrix(renderShader, matViewLoc, matView);

  rlBindShaderBuffer(ssboParticleId, 0);
  DrawMeshInstanced(quadMesh, particleMaterial, dummyInstancedTransforms,
                    MAX_GLOBAL_PARTICLES);

  EndBlendMode();
  rlEnableDepthMask();
}

bool IsParticleSystemActive(void) { return true; }

void UnloadParticleSystem(void) {
  UnloadShader(renderShader);
  rlUnloadShaderProgram(computeProgramId);
  rlUnloadShaderBuffer(ssboParticleId);
  UnloadMesh(quadMesh);
  free(dummyInstancedTransforms);
}

#endif // __APPLE__