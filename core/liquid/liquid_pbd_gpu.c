#include "core/liquid/liquid_pbd_gpu.h"
#include <stdlib.h>
#include "rlgl.h"
#include "raymath.h"
#include "core/resource_manager.h"
#include "core/gfx_quality.h"
#include "core/liquid/liquid_surface.h"
#include <math.h>
#include <string.h>

#define GPU_GRID_CELLS (32*32*32)
#define LIQUID_PBD_LIFETIME 2.50f

/* Three vec4s: keep the C stride identical to GLSL std430. */
typedef struct {
    float px, py, pz, radius;
    float vx, vy, vz, life;
    float phase, seed, hasImpacted, pad;
} GPULiquidParticle;

typedef struct {
    int materialId;
    int matchFrontMaterial;
    int frontDepthTex;
    int view;
    int projection;
} PBDSurfaceUniforms;

static unsigned int s_stateA, s_stateB, s_heads, s_next, s_program;
static bool s_active;
static bool s_initAttempted;
static int s_particleCount;
static Vector3 s_receiverPoint, s_receiverNormal;
static float s_impactAge;
static int s_materialSlot;   /* liquid-table slot, captured at spawn */
static float s_gridCellSize;
static unsigned int s_vao, s_vbo;

static struct {
    int phase, particleCount, gridCellCount, solverIteration;
    int dt, cellSize, impactAge, gridStamp;
    int receiverPoint, receiverNormal, gridOrigin;
} s_uniform;

static PBDSurfaceUniforms s_depthUni, s_backUni;

static int s_gridStamp;
#define PBD_STAMP_WRAP 500000     /* re-clear well before int overflow at x4096 */
static void LiquidPBDGPU_ClearGrid(void);
static Shader s_depthShader, s_backShader;
static Texture2D s_frontDepth;
static float s_reconstructionRadius = 0.022f;

void LiquidPBDGPU_SetSurfaceFrontDepth(Texture2D frontDepth) { s_frontDepth = frontDepth; }

static void LiquidPBDGPU_CacheSurfaceUniforms(Shader shader, PBDSurfaceUniforms *out)
{
    out->materialId = GetShaderLocation(shader, "u_materialId");
    out->matchFrontMaterial = GetShaderLocation(shader, "u_matchFrontMaterial");
    out->frontDepthTex = GetShaderLocation(shader, "u_frontDepthTex");
    out->view = GetShaderLocation(shader, "u_view");
    out->projection = GetShaderLocation(shader, "u_projection");
}

static float LiquidPBDGPU_Rand01(unsigned int value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return (float)(value & 0x00ffffffU) / 16777216.0f;
}

static unsigned int LiquidPBDGPU_LoadCompute(void)
{
    char *src = LoadFileText("core/liquid/shaders/liquid_pbd_gpu.comp");
    if (!src) return 0;
    unsigned int shader = rlLoadShader(src, RL_COMPUTE_SHADER);
    UnloadFileText(src);
    if (!shader) return 0;
    unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    return program;
}

bool LiquidPBDGPU_Init(void)
{
    if (s_active) return true;
    if (s_initAttempted) return false;
    s_initAttempted = true;
    const char *disabled = getenv("WUXING_LIQUID_PBD_DISABLE");
    if (disabled && atoi(disabled) != 0) {
        TraceLog(LOG_INFO, "LIQUID_PBD: disabled for fallback validation");
        return false;
    }
    s_program = LiquidPBDGPU_LoadCompute();
    if (!s_program) return false;
    s_stateA = rlLoadShaderBuffer(sizeof(GPULiquidParticle) * LIQUID_PBD_GPU_MAX_PARTICLES, NULL, RL_DYNAMIC_DRAW);
    s_stateB = rlLoadShaderBuffer(sizeof(GPULiquidParticle) * LIQUID_PBD_GPU_MAX_PARTICLES, NULL, RL_DYNAMIC_DRAW);
    s_heads = rlLoadShaderBuffer(sizeof(int) * GPU_GRID_CELLS, NULL, RL_DYNAMIC_DRAW);
    s_next = rlLoadShaderBuffer(sizeof(int) * LIQUID_PBD_GPU_MAX_PARTICLES, NULL, RL_DYNAMIC_DRAW);
    s_depthShader = ResourceManager_LoadShader("core/liquid/shaders/liquid_pbd_surface.vs",
                                               "core/liquid/shaders/liquid_capture_particle.fs");
    s_backShader = ResourceManager_LoadShader("core/liquid/shaders/liquid_pbd_surface.vs",
                                              "core/liquid/shaders/liquid_capture_particle_back.fs");
    static const float quad[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0};
    s_vao = rlLoadVertexArray();
    rlEnableVertexArray(s_vao);
    s_vbo = rlLoadVertexBuffer(quad, sizeof(quad), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(0);
    rlDisableVertexArray();
    if (!s_stateA || !s_stateB || !s_heads || !s_next || !s_vao || !s_vbo ||
        !s_depthShader.id || !s_backShader.id ||
        s_depthShader.id == rlGetShaderIdDefault() || s_backShader.id == rlGetShaderIdDefault()) {
        LiquidPBDGPU_Unload();
        s_initAttempted = true; /* cache failure; Force Field owns live fallback */
        return false;
    }
    s_uniform.phase = rlGetLocationUniform(s_program, "u_phase");
    s_uniform.particleCount = rlGetLocationUniform(s_program, "u_particleCount");
    s_uniform.gridCellCount = rlGetLocationUniform(s_program, "u_gridCellCount");
    s_uniform.solverIteration = rlGetLocationUniform(s_program, "u_solverIteration");
    s_uniform.dt = rlGetLocationUniform(s_program, "u_dt");
    s_uniform.cellSize = rlGetLocationUniform(s_program, "u_cellSize");
    s_uniform.impactAge = rlGetLocationUniform(s_program, "u_impactAge");
    s_uniform.gridStamp = rlGetLocationUniform(s_program, "u_gridStamp");
    s_uniform.receiverPoint = rlGetLocationUniform(s_program, "u_receiverPoint");
    s_uniform.receiverNormal = rlGetLocationUniform(s_program, "u_receiverNormal");
    s_uniform.gridOrigin = rlGetLocationUniform(s_program, "u_gridOrigin");

    LiquidPBDGPU_CacheSurfaceUniforms(s_depthShader, &s_depthUni);
    LiquidPBDGPU_CacheSurfaceUniforms(s_backShader, &s_backUni);

    s_active = s_stateA && s_stateB && s_heads && s_next;
    return s_active;
}

int LiquidPBDGPU_GetMaterial(void) { return s_materialSlot; }
float LiquidPBDGPU_GetReconstructionRadius(void) { return s_reconstructionRadius; }
bool LiquidPBDGPU_IsActive(void) { return s_active && s_particleCount > 0; }

void LiquidPBDGPU_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    if (!s_active) return;
    GPULiquidParticle particles[LIQUID_PBD_GPU_MAX_PARTICLES];
    GfxQuality quality = GfxQuality_Get();
    int count = quality <= GFX_LOW ? 1024 : (quality >= GFX_HIGH ? 2048 : 1536);
    if (count > LIQUID_PBD_GPU_MAX_PARTICLES) count = LIQUID_PBD_GPU_MAX_PARTICLES;
    s_particleCount = count;

    s_reconstructionRadius = scale * 0.022f;
    float radius = scale * (quality <= GFX_LOW ? 0.010f : (quality >= GFX_HIGH ? 0.0085f : 0.0092f));
    s_gridCellSize = fmaxf(scale, 0.10f) * 0.046f;
    normal = Vector3LengthSqr(normal) > 0.00001f ? Vector3Normalize(normal) : (Vector3){0.0f, 1.0f, 0.0f};
    s_receiverPoint = point;
    s_receiverNormal = normal;
    s_impactAge = 0.0f;
    s_materialSlot = LiquidSurface_CurrentMaterial();

    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 tangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    Vector3 bitangent = Vector3Normalize(Vector3CrossProduct(normal, tangent));

    float volumeRadius = scale * (0.145f + 0.035f * force01);
    float speed = Vector3Length(impulse);
    Vector3 travelDir = speed > 0.001f ? Vector3Scale(impulse, 1.0f / speed) : Vector3Negate(normal);
    float preImpactDistance = Clamp(speed * 0.055f, scale * 0.18f, scale * 0.45f);
    Vector3 seedCenter = Vector3Subtract(point, Vector3Scale(travelDir, preImpactDistance));

    for (int i = 0; i < count; i++) {
        unsigned int seed = (unsigned int)i * 747796405U + 2891336453U;
        float z = 1.0f - 2.0f * LiquidPBDGPU_Rand01(seed);
        float a = LiquidPBDGPU_Rand01(seed + 1U) * 2.0f * PI;
        float shell = cbrtf(LiquidPBDGPU_Rand01(seed + 2U));
        float xy = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        Vector3 volumeDirection = Vector3Add(Vector3Add(Vector3Scale(tangent, cosf(a) * xy),
            Vector3Scale(bitangent, sinf(a) * xy)), Vector3Scale(normal, z));
        Vector3 offset = Vector3Scale(volumeDirection, shell * volumeRadius);

        float axial = Vector3DotProduct(offset, travelDir);
        Vector3 lateral = Vector3Subtract(offset, Vector3Scale(travelDir, axial));
        float lobe = 0.82f + 0.30f * LiquidPBDGPU_Rand01(seed + 3U);
        offset = Vector3Add(Vector3Scale(lateral, lobe),
                            Vector3Scale(travelDir, axial * 0.58f));
        Vector3 position = Vector3Add(seedCenter, offset);
        Vector3 velocity = impulse;
        float phase = shell < 0.749f ? 0.0f : (shell < 0.973f ? 1.0f : 2.0f);
        float particleSeed = LiquidPBDGPU_Rand01(seed + 5U);
        float physicalScale = 0.80f + 0.40f * LiquidPBDGPU_Rand01(seed + 6U);
        float particleRadius = radius * physicalScale;
        float opticalJitter = LiquidPBDGPU_Rand01(seed + 7U);
        particles[i] = (GPULiquidParticle){
            position.x, position.y, position.z, particleRadius,
            velocity.x, velocity.y, velocity.z, LIQUID_PBD_LIFETIME,
            phase, particleSeed, 0.0f, opticalJitter
        };
    }
    rlUpdateShaderBuffer(s_stateA, particles, sizeof(GPULiquidParticle) * count, 0);
    rlUpdateShaderBuffer(s_stateB, particles, sizeof(GPULiquidParticle) * count, 0);
    LiquidPBDGPU_ClearGrid();
}

static void LiquidPBDGPU_ClearGrid(void)
{
    int phase = 0, cells = GPU_GRID_CELLS;
    rlEnableShader(s_program);
    rlBindShaderBuffer(s_heads, 2);
    rlSetUniform(s_uniform.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_uniform.gridCellCount, &cells, RL_SHADER_UNIFORM_INT, 1);
    rlComputeShaderDispatch((cells + 255) / 256, 1, 1);
    rlDisableShader();
    s_gridStamp = 1;
}

void LiquidPBDGPU_Update(float dt, float groundY)
{
    (void)groundY;
    if (!s_active || s_particleCount <= 0 || dt <= 0.0f) return;
    s_impactAge += dt;
    if (s_impactAge >= LIQUID_PBD_LIFETIME) { s_particleCount = 0; return; }

    /* Clamp integration timestep so frame hitches cannot tunnel through the receiver plane */
    float simDt = fminf(dt, 1.0f / 30.0f);
    unsigned int groups = (s_particleCount + 255) / 256;
    int phase, count = s_particleCount, cells = GPU_GRID_CELLS;
    float cell = s_gridCellSize;

    if (s_gridStamp > PBD_STAMP_WRAP) LiquidPBDGPU_ClearGrid();

    rlEnableShader(s_program);
    rlBindShaderBuffer(s_stateA, 0);
    rlBindShaderBuffer(s_stateB, 1);
    rlBindShaderBuffer(s_heads, 2);
    rlBindShaderBuffer(s_next, 3);
    rlSetUniform(s_uniform.gridCellCount, &cells, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_uniform.particleCount, &count, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_uniform.dt, &simDt, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_uniform.cellSize, &cell, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_uniform.receiverPoint, &s_receiverPoint.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_uniform.receiverNormal, &s_receiverNormal.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_uniform.gridOrigin, &s_receiverPoint.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_uniform.impactAge, &s_impactAge, RL_SHADER_UNIFORM_FLOAT, 1);

    s_gridStamp++;
    rlSetUniform(s_uniform.gridStamp, &s_gridStamp, RL_SHADER_UNIFORM_INT, 1);
    phase = 1;
    rlSetUniform(s_uniform.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
    rlComputeShaderDispatch(groups, 1, 1);

    unsigned int t = s_stateA;
    s_stateA = s_stateB;
    s_stateB = t;
    rlBindShaderBuffer(s_stateA, 0);
    rlBindShaderBuffer(s_stateB, 1);

    int baseIterations = GfxQuality_Get() <= GFX_LOW ? 2 : (GfxQuality_Get() >= GFX_HIGH ? 4 : 3);
    int iterations = baseIterations;
    if (s_impactAge >= 1.24f) iterations = 0;
    else if (s_impactAge >= 0.85f) iterations = 1;
    else if (s_impactAge >= 0.45f && iterations > 2) iterations = 2;

    for (int i = 0; i < iterations; i++) {
        s_gridStamp++;
        rlSetUniform(s_uniform.gridStamp, &s_gridStamp, RL_SHADER_UNIFORM_INT, 1);
        phase = 3;
        rlSetUniform(s_uniform.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
        rlComputeShaderDispatch(groups, 1, 1);
        phase = 2;
        rlSetUniform(s_uniform.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniform(s_uniform.solverIteration, &i, RL_SHADER_UNIFORM_INT, 1);
        rlComputeShaderDispatch(groups, 1, 1);
        t = s_stateA;
        s_stateA = s_stateB;
        s_stateB = t;
        rlBindShaderBuffer(s_stateA, 0);
        rlBindShaderBuffer(s_stateB, 1);
    }
    rlDisableShader();
}

void LiquidPBDGPU_Unload(void)
{
    if (s_stateA) rlUnloadShaderBuffer(s_stateA);
    if (s_stateB) rlUnloadShaderBuffer(s_stateB);
    if (s_heads) rlUnloadShaderBuffer(s_heads);
    if (s_next) rlUnloadShaderBuffer(s_next);
    if (s_program) rlUnloadShaderProgram(s_program);
    if (s_vbo) rlUnloadVertexBuffer(s_vbo);
    if (s_vao) rlUnloadVertexArray(s_vao);
    s_stateA = s_stateB = s_heads = s_next = s_program = s_vbo = s_vao = 0;
    s_active = false;
    s_initAttempted = false;
    s_particleCount = 0;
    s_frontDepth = (Texture2D){0};
}

unsigned int LiquidPBDGPU_GetStateBuffer(void) { return s_stateA; }
int LiquidPBDGPU_GetParticleCount(void) { return s_particleCount; }

static void LiquidPBDGPU_Draw(Camera3D camera, Shader shader, const PBDSurfaceUniforms *uni)
{
    if (!s_active || s_particleCount <= 0) return;
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    Matrix projection;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy * 0.5;
        projection = MatrixOrtho(-top * aspect, top * aspect, -top, top, 0.0001, 150.0);
    } else {
        double top = tan(camera.fovy * 0.5 * DEG2RAD);
        projection = MatrixFrustum(-top * aspect, top * aspect, -top, top, 1.0, 1000.0);
    }
    BeginShaderMode(shader);
    float material = (float)LiquidPBDGPU_GetMaterial();
    if (uni->materialId >= 0)
        SetShaderValue(shader, uni->materialId, &material, SHADER_UNIFORM_FLOAT);
    float match = s_frontDepth.id ? 1.0f : 0.0f;
    if (uni->matchFrontMaterial >= 0) {
        SetShaderValue(shader, uni->matchFrontMaterial, &match, SHADER_UNIFORM_FLOAT);
        int slot = 0;
        if (uni->frontDepthTex >= 0)
            SetShaderValue(shader, uni->frontDepthTex, &slot, SHADER_UNIFORM_INT);
        rlActiveTextureSlot(0);
        if (match > 0.5f) rlEnableTexture(s_frontDepth.id);
    }
    if (uni->view >= 0) SetShaderValueMatrix(shader, uni->view, view);
    if (uni->projection >= 0) SetShaderValueMatrix(shader, uni->projection, projection);
    rlBindShaderBuffer(s_stateA, 0);
    rlEnableShader(shader.id);
    rlEnableVertexArray(s_vao);
    rlDrawVertexArrayInstanced(0, 6, s_particleCount);
    rlDisableVertexArray();
    rlDisableShader();
    if (uni->matchFrontMaterial >= 0 && match > 0.5f) rlDisableTexture();
    EndShaderMode();
}

void LiquidPBDGPU_DrawSurfaceDepth(Camera3D camera)
{
    LiquidPBDGPU_Draw(camera, s_depthShader, &s_depthUni);
}

void LiquidPBDGPU_DrawSurfaceBackDepth(Camera3D camera)
{
    LiquidPBDGPU_Draw(camera, s_backShader, &s_backUni);
}
