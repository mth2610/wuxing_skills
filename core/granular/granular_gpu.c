#include "core/granular/granular_gpu.h"
#include "core/gfx_quality.h"
#include "core/motion/motion_gpu.h"
#include "core/resource_manager.h"
#include "core/shading/shader_preprocessor.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define GRANULAR_GPU_GRID_CELLS (32 * 32 * 32)
#define GRANULAR_STAMP_WRAP     500000

typedef struct {
    float px, py, pz, radius;
    float vx, vy, vz, life;
    float seed, materialId, contactAO, flags;
    float shearWork, pad0, pad1, pad2;
} GranularGpuGrain;

typedef struct {
    float originX, originY, originZ, radius;
    float dirX, dirY, dirZ, topRadius;
    float length, axialSpeed, waveSpeed, swirlSpeed;
    float inwardPull, liftAccel, analyticLock, duration;
    float attackTime, fadeTime, elapsed, kind;
    float loopTop, stormVelX, stormVelY, stormVelZ;
    float spinDown, pad0, pad1, pad2;
} GranularGpuWave;

static unsigned int s_stateA = 0, s_stateB = 0, s_heads = 0, s_next = 0, s_program = 0;
static unsigned int s_waveSsbo = 0, s_motionSceneSsbo = 0, s_motionBodySsbo = 0;
static MotionGpuScene s_motionScene;
static GranularGpuWave s_waveUpload[GRANULAR_MAX_ACTIVE_WAVES];
static int s_waveCount = 0;
static float s_elapsedTime = 0.0f;
static unsigned int s_vao = 0, s_vbo = 0;
static Shader s_renderShader = {0};
static bool s_available = false;
static bool s_initAttempted = false;
static int s_grainCount = 0;
static int s_gridStamp = 1;
static float s_cellSize = 0.06f;
static Vector3 s_receiverPoint = {0.0f, 0.0f, 0.0f};
static Vector3 s_receiverNormal = {0.0f, 1.0f, 0.0f};
static GranularMaterialProfile s_activeMat = {0};
static float s_maxLifeRemaining = 0.0f;

static Vector3 s_impulseCenter = {0};
static float s_impulseRadius = 0.0f;
static Vector3 s_impulseAccel = {0};
static float s_impulseSwirl = 0.0f;

static int s_funnelActive = 0;
static Vector3 s_funnelBase = {0};
static Vector3 s_funnelPrevBase = {0};
static bool s_funnelHasPrevBase = false;
static Vector3 s_funnelVelocity = {0};
static float s_funnelSpinDown = 0.0f;
static float s_funnelSpinDownTimer = 0.0f;
static float s_funnelBaseRadius = 0.42f;
static float s_funnelTopRadius = 1.65f;
static float s_funnelHeight = 3.6f;
static float s_funnelSwirlSpeed = 13.5f;
static float s_funnelUpdraftSpeed = 3.4f;

static struct {
    int phase, grainCount, gridCellCount, gridStamp;
    int dt, time, cellSize, receiverPoint, receiverNormal, gridOrigin;
    int springStiffness, dashpotDamping, shearFrictionMu, rollingResistance;
    int cohesion, airDrag, saltationSplash;
    int impulseCenter, impulseRadius, impulseAccel, impulseSwirl;
    int funnelActive, funnelBase, funnelVelocity, funnelSpinDown;
    int funnelBaseRadius, funnelTopRadius;
    int funnelHeight, funnelSwirlSpeed, funnelUpdraftSpeed;
    int waveCount;
} s_compUni;

static struct {
    int view, projection, aspectRatio, time;
    int baseColor, shadowColor, highlightColor;
    int glintIntensity, glintSharpness, lightDir, camPos;
} s_drawUni;

static unsigned int GranularGpu_LoadCompute(void)
{
    char *src = ShaderPreprocessor_Load("core/granular/shaders/granular_dem.comp");
    if (!src) return 0;
    unsigned int shader = rlLoadShader(src, RL_COMPUTE_SHADER);
    RL_FREE(src);
    if (!shader) return 0;
    unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    return program;
}

static void GranularGpu_ClearGrid(void)
{
    if (!s_program || !s_heads) return;
    int phase = 0;
    int cells = GRANULAR_GPU_GRID_CELLS;
    rlEnableShader(s_program);
    rlBindShaderBuffer(s_heads, 2);
    rlSetUniform(s_compUni.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_compUni.gridCellCount, &cells, RL_SHADER_UNIFORM_INT, 1);
    rlComputeShaderDispatch((cells + 255) / 256, 1, 1);
    rlDisableShader();
    s_gridStamp = 1;
}

bool GranularGpu_Init(void)
{
    if (s_available) return true;
    if (s_initAttempted) return false;
    s_initAttempted = true;

    const char *disabled = getenv("WUXING_GRANULAR_GPU_DISABLE");
    if (disabled && atoi(disabled) != 0) return false;

    s_program = GranularGpu_LoadCompute();
    if (!s_program) return false;

    s_stateA = rlLoadShaderBuffer(sizeof(GranularGpuGrain) * GRANULAR_GPU_MAX_GRAINS, NULL, RL_DYNAMIC_DRAW);
    s_stateB = rlLoadShaderBuffer(sizeof(GranularGpuGrain) * GRANULAR_GPU_MAX_GRAINS, NULL, RL_DYNAMIC_DRAW);
    s_heads  = rlLoadShaderBuffer(sizeof(int) * GRANULAR_GPU_GRID_CELLS, NULL, RL_DYNAMIC_DRAW);
    s_next   = rlLoadShaderBuffer(sizeof(int) * GRANULAR_GPU_MAX_GRAINS, NULL, RL_DYNAMIC_DRAW);
    s_waveSsbo        = rlLoadShaderBuffer(sizeof(GranularGpuWave) * GRANULAR_MAX_ACTIVE_WAVES, NULL, RL_DYNAMIC_DRAW);
    s_motionSceneSsbo = rlLoadShaderBuffer(sizeof(MotionGpuScene), NULL, RL_DYNAMIC_DRAW);
    s_motionBodySsbo  = rlLoadShaderBuffer(sizeof(MotionGpuBody) * GRANULAR_GPU_MAX_GRAINS, NULL, RL_DYNAMIC_DRAW);

    s_renderShader = ResourceManager_LoadShader(
        "core/granular/shaders/granular_grain.vs",
        "core/granular/shaders/granular_grain.fs");

    // Unified 36-vertex (12-triangle) template VBO:
    // First 6 verts carry standard [-1..1]^2 quad corners in .xy (for fine continuum splats)
    // and .z carries vertex index 0..35 for true 3D procedural sub-meshes.
    static float tmpl[36 * 3];
    static const float quadXY[6][2] = {
        {-1.0f, -1.0f}, { 1.0f, -1.0f}, { 1.0f,  1.0f},
        {-1.0f, -1.0f}, { 1.0f,  1.0f}, {-1.0f,  1.0f}
    };
    for (int v = 0; v < 36; v++) {
        tmpl[v * 3 + 0] = (v < 6) ? quadXY[v][0] : 0.0f;
        tmpl[v * 3 + 1] = (v < 6) ? quadXY[v][1] : 0.0f;
        tmpl[v * 3 + 2] = (float)v;
    }
    s_vao = rlLoadVertexArray();
    rlEnableVertexArray(s_vao);
    s_vbo = rlLoadVertexBuffer(tmpl, sizeof(tmpl), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(0);
    rlDisableVertexArray();

    if (!s_stateA || !s_stateB || !s_heads || !s_next || !s_waveSsbo ||
        !s_motionSceneSsbo || !s_motionBodySsbo ||
        !s_vao || !s_vbo || !s_renderShader.id || s_renderShader.id == rlGetShaderIdDefault()) {
        GranularGpu_Unload();
        s_initAttempted = true;
        return false;
    }

    s_compUni.phase             = rlGetLocationUniform(s_program, "u_phase");
    s_compUni.grainCount        = rlGetLocationUniform(s_program, "u_grainCount");
    s_compUni.gridCellCount     = rlGetLocationUniform(s_program, "u_gridCellCount");
    s_compUni.gridStamp         = rlGetLocationUniform(s_program, "u_gridStamp");
    s_compUni.dt                = rlGetLocationUniform(s_program, "u_dt");
    s_compUni.time              = rlGetLocationUniform(s_program, "u_time");
    s_compUni.cellSize          = rlGetLocationUniform(s_program, "u_cellSize");
    s_compUni.receiverPoint     = rlGetLocationUniform(s_program, "u_receiverPoint");
    s_compUni.receiverNormal    = rlGetLocationUniform(s_program, "u_receiverNormal");
    s_compUni.gridOrigin        = rlGetLocationUniform(s_program, "u_gridOrigin");
    s_compUni.springStiffness   = rlGetLocationUniform(s_program, "u_springStiffness");
    s_compUni.dashpotDamping    = rlGetLocationUniform(s_program, "u_dashpotDamping");
    s_compUni.shearFrictionMu   = rlGetLocationUniform(s_program, "u_shearFrictionMu");
    s_compUni.rollingResistance = rlGetLocationUniform(s_program, "u_rollingResistance");
    s_compUni.cohesion          = rlGetLocationUniform(s_program, "u_cohesion");
    s_compUni.airDrag           = rlGetLocationUniform(s_program, "u_airDrag");
    s_compUni.saltationSplash   = rlGetLocationUniform(s_program, "u_saltationSplash");
    s_compUni.impulseCenter     = rlGetLocationUniform(s_program, "u_impulseCenter");
    s_compUni.impulseRadius     = rlGetLocationUniform(s_program, "u_impulseRadius");
    s_compUni.impulseAccel      = rlGetLocationUniform(s_program, "u_impulseAccel");
    s_compUni.impulseSwirl      = rlGetLocationUniform(s_program, "u_impulseSwirl");
    s_compUni.funnelActive       = rlGetLocationUniform(s_program, "u_funnelActive");
    s_compUni.funnelBase         = rlGetLocationUniform(s_program, "u_funnelBase");
    s_compUni.funnelVelocity     = rlGetLocationUniform(s_program, "u_funnelVelocity");
    s_compUni.funnelSpinDown     = rlGetLocationUniform(s_program, "u_funnelSpinDown");
    s_compUni.funnelBaseRadius   = rlGetLocationUniform(s_program, "u_funnelBaseRadius");
    s_compUni.funnelTopRadius    = rlGetLocationUniform(s_program, "u_funnelTopRadius");
    s_compUni.funnelHeight       = rlGetLocationUniform(s_program, "u_funnelHeight");
    s_compUni.funnelSwirlSpeed   = rlGetLocationUniform(s_program, "u_funnelSwirlSpeed");
    s_compUni.funnelUpdraftSpeed = rlGetLocationUniform(s_program, "u_funnelUpdraftSpeed");
    s_compUni.waveCount          = rlGetLocationUniform(s_program, "u_waveCount");

    s_drawUni.view           = GetShaderLocation(s_renderShader, "u_view");
    s_drawUni.projection     = GetShaderLocation(s_renderShader, "u_projection");
    s_drawUni.aspectRatio    = GetShaderLocation(s_renderShader, "u_aspectRatio");
    s_drawUni.time           = GetShaderLocation(s_renderShader, "u_time");
    s_drawUni.baseColor      = GetShaderLocation(s_renderShader, "u_baseColor");
    s_drawUni.shadowColor    = GetShaderLocation(s_renderShader, "u_shadowColor");
    s_drawUni.highlightColor = GetShaderLocation(s_renderShader, "u_highlightColor");
    s_drawUni.glintIntensity = GetShaderLocation(s_renderShader, "u_glintIntensity");
    s_drawUni.glintSharpness = GetShaderLocation(s_renderShader, "u_glintSharpness");
    s_drawUni.lightDir       = GetShaderLocation(s_renderShader, "u_lightDir");
    s_drawUni.camPos         = GetShaderLocation(s_renderShader, "u_camPos");

    s_available = true;
    return true;
}

void GranularGpu_Unload(void)
{
    if (s_stateA)  rlUnloadShaderBuffer(s_stateA);
    if (s_stateB)  rlUnloadShaderBuffer(s_stateB);
    if (s_heads)           rlUnloadShaderBuffer(s_heads);
    if (s_next)            rlUnloadShaderBuffer(s_next);
    if (s_waveSsbo)        rlUnloadShaderBuffer(s_waveSsbo);
    if (s_motionSceneSsbo) rlUnloadShaderBuffer(s_motionSceneSsbo);
    if (s_motionBodySsbo)  rlUnloadShaderBuffer(s_motionBodySsbo);
    if (s_program) rlUnloadShaderProgram(s_program);
    if (s_vbo)     rlUnloadVertexBuffer(s_vbo);
    if (s_vao)     rlUnloadVertexArray(s_vao);
    s_stateA = s_stateB = s_heads = s_next = s_waveSsbo = s_motionSceneSsbo = s_motionBodySsbo = s_program = s_vbo = s_vao = 0;
    s_available = false;
    s_initAttempted = false;
    s_grainCount = 0;
    s_maxLifeRemaining = 0.0f;
}

bool GranularGpu_IsAvailable(void) { return s_available; }
int GranularGpu_GetGrainCount(void) { return s_grainCount; }
void GranularGpu_Clear(void) { s_grainCount = 0; s_maxLifeRemaining = 0.0f; s_waveCount = 0; }

static MotionGpuBody GranularGpu_PackBodyForGrain(const GranularGrainState *g,
                                                  const GranularMaterialProfile *fallbackMat)
{
    const GranularMaterialProfile *gm = GranularSim_GetMaterialProfile((GranularMaterialId)g->materialId);
    if (!gm) gm = fallbackMat;
    float massKg = (gm && gm->referenceMassKg > 1e-5f) ? gm->referenceMassKg : 0.012f;
    float r = g->radius > 0.005f ? g->radius : 0.02f;
    bool isLamina = gm && (gm->morphology == GRANULAR_MORPH_PLANAR_LAMINA ||
                           gm->morphology == GRANULAR_MORPH_ELONGATED_TWIG);
    unsigned int recvMask = MOTION_RECEIVER_PARTICLE | (isLamina ? MOTION_RECEIVER_FOLIAGE : 0u);
    MotionBodyProfile bodyProf = {
        .inverseMassKg = 1.0f / massKg,
        .gravityScale = 1.0f,
        .linearDragPerSecond = gm ? gm->airDrag : 0.45f,
        .terminalSpeedMps = 28.0f,
        .windAccelerationScale = 1.0f,
        .windCouplingHz = isLamina ? 5.5f : 3.5f,
        .windSusceptibility = isLamina ? 1.0f : 0.65f,
        .steeringFrequencyHz = 0.0f,
        .aerodynamicAreaM2 = 3.14159265f * r * r * (isLamina ? 1.8f : 1.0f),
        .aerodynamicDragCoefficient = isLamina ? 1.28f : 0.47f,
        .airDensityKgM3 = 1.225f,
        .densityKgM3 = gm ? gm->densityKgM3 : 1600.0f
    };
    return MotionGpu_PackBodyForReceiver(&bodyProf, (Vector3){0}, (Vector3){0}, false,
                                         bodyProf.windSusceptibility, recvMask);
}

void GranularGpu_UploadGrains(const GranularGrainState *grains, int count,
                              Vector3 receiverPoint, Vector3 receiverNormal,
                              float cellSize, const GranularMaterialProfile *mat)
{
    if (!s_available || !grains || count <= 0) return;
    if (count > GRANULAR_GPU_MAX_GRAINS) count = GRANULAR_GPU_MAX_GRAINS;

    static GranularGpuGrain s_uploadBuf[GRANULAR_GPU_MAX_GRAINS];
    static MotionGpuBody s_bodyUploadBuf[GRANULAR_GPU_MAX_GRAINS];
    float maxLife = 0.0f;
    for (int i = 0; i < count; i++) {
        if (grains[i].lifetime > maxLife) maxLife = grains[i].lifetime;
        s_uploadBuf[i] = (GranularGpuGrain){
            .px = grains[i].position.x,
            .py = grains[i].position.y,
            .pz = grains[i].position.z,
            .radius = grains[i].radius,
            .vx = grains[i].velocity.x,
            .vy = grains[i].velocity.y,
            .vz = grains[i].velocity.z,
            .life = grains[i].lifetime,
            .seed = grains[i].seed,
            .materialId = (float)grains[i].materialId,
            .contactAO = grains[i].contactAO,
            .flags = (float)grains[i].flags,
            .shearWork = 0.0f,
            .pad0 = grains[i].seed * 6.2831853f, .pad1 = 2.0f + grains[i].seed * 3.0f, .pad2 = 0.0f
        };
        s_bodyUploadBuf[i] = GranularGpu_PackBodyForGrain(&grains[i], mat);
    }

    s_grainCount = count;
    s_maxLifeRemaining = maxLife;
    s_receiverPoint = receiverPoint;
    s_receiverNormal = Vector3LengthSqr(receiverNormal) > 1e-5f
        ? Vector3Normalize(receiverNormal) : (Vector3){0.0f, 1.0f, 0.0f};
    s_cellSize = fmaxf(cellSize, 0.04f);
    if (mat) s_activeMat = *mat;

    rlUpdateShaderBuffer(s_stateA, s_uploadBuf, (unsigned int)(sizeof(GranularGpuGrain) * count), 0);
    rlUpdateShaderBuffer(s_stateB, s_uploadBuf, (unsigned int)(sizeof(GranularGpuGrain) * count), 0);
    rlUpdateShaderBuffer(s_motionBodySsbo, s_bodyUploadBuf, (unsigned int)(sizeof(MotionGpuBody) * count), 0);
    GranularGpu_ClearGrid();
}

int GranularGpu_AppendGrains(const GranularGrainState *grains, int count,
                             Vector3 receiverPoint, Vector3 receiverNormal,
                             float cellSize)
{
    if (!s_available || !grains || count <= 0) return 0;
    int cap = GfxQuality_Get() <= GFX_LOW ? 1024 : (GfxQuality_Get() >= GFX_HIGH ? GRANULAR_GPU_MAX_GRAINS : 2048);
    if (s_grainCount <= 0) {
        int spawnN = count > cap ? cap : count;
        const GranularMaterialProfile *mat = GranularSim_GetMaterialProfile((GranularMaterialId)grains[0].materialId);
        GranularGpu_UploadGrains(grains, spawnN, receiverPoint, receiverNormal, cellSize, mat);
        return spawnN;
    }

    int avail = cap - s_grainCount;
    if (avail <= 0) return 0;
    int addN = count > avail ? avail : count;

    static GranularGpuGrain s_appendBuf[GRANULAR_GPU_MAX_GRAINS];
    static MotionGpuBody s_bodyAppendBuf[GRANULAR_GPU_MAX_GRAINS];
    for (int i = 0; i < addN; i++) {
        if (grains[i].lifetime > s_maxLifeRemaining) s_maxLifeRemaining = grains[i].lifetime;
        s_appendBuf[i] = (GranularGpuGrain){
            .px = grains[i].position.x,
            .py = grains[i].position.y,
            .pz = grains[i].position.z,
            .radius = grains[i].radius,
            .vx = grains[i].velocity.x,
            .vy = grains[i].velocity.y,
            .vz = grains[i].velocity.z,
            .life = grains[i].lifetime,
            .seed = grains[i].seed,
            .materialId = (float)grains[i].materialId,
            .contactAO = grains[i].contactAO,
            .flags = (float)grains[i].flags,
            .shearWork = 0.0f,
            .pad0 = grains[i].seed * 6.2831853f, .pad1 = 2.0f + grains[i].seed * 3.0f, .pad2 = 0.0f
        };
        s_bodyAppendBuf[i] = GranularGpu_PackBodyForGrain(&grains[i], &s_activeMat);
    }

    unsigned int byteOff = (unsigned int)(sizeof(GranularGpuGrain) * s_grainCount);
    unsigned int bodyOff = (unsigned int)(sizeof(MotionGpuBody) * s_grainCount);
    rlUpdateShaderBuffer(s_stateA, s_appendBuf, (unsigned int)(sizeof(GranularGpuGrain) * addN), byteOff);
    rlUpdateShaderBuffer(s_stateB, s_appendBuf, (unsigned int)(sizeof(GranularGpuGrain) * addN), byteOff);
    rlUpdateShaderBuffer(s_motionBodySsbo, s_bodyAppendBuf, (unsigned int)(sizeof(MotionGpuBody) * addN), bodyOff);
    s_grainCount += addN;
    if (cellSize > s_cellSize) s_cellSize = cellSize;
    return addN;
}

void GranularGpu_SetWaves(const GranularWaveDesc *waves, int waveCount)
{
    if (!waves || waveCount <= 0) {
        s_waveCount = 0;
        return;
    }
    int n = waveCount > GRANULAR_MAX_ACTIVE_WAVES ? GRANULAR_MAX_ACTIVE_WAVES : waveCount;
    s_waveCount = n;
    for (int i = 0; i < n; i++) {
        const GranularWaveDesc *w = &waves[i];
        s_waveUpload[i] = (GranularGpuWave){
            .originX = w->origin.x, .originY = w->origin.y, .originZ = w->origin.z, .radius = w->radius,
            .dirX = w->direction.x, .dirY = w->direction.y, .dirZ = w->direction.z, .topRadius = w->topRadius,
            .length = w->length, .axialSpeed = w->axialSpeed, .waveSpeed = w->waveSpeed, .swirlSpeed = w->swirlSpeed,
            .inwardPull = w->inwardPull, .liftAccel = w->liftAccel, .analyticLock = w->analyticLock, .duration = w->duration,
            .attackTime = w->attackTime, .fadeTime = w->fadeTime, .elapsed = w->elapsed, .kind = (float)w->kind,
            .loopTop = w->loopTop ? 1.0f : 0.0f,
            .stormVelX = w->stormVelocity.x, .stormVelY = w->stormVelocity.y, .stormVelZ = w->stormVelocity.z,
            .spinDown = w->spinDown, .pad0 = 0.0f, .pad1 = 0.0f, .pad2 = 0.0f
        };
    }
}

void GranularGpu_SetImpulse(Vector3 center, float radius,
                            Vector3 accel, float swirlRadPerSec)
{
    s_impulseCenter = center;
    s_impulseRadius = radius;
    s_impulseAccel = accel;
    s_impulseSwirl = swirlRadPerSec;
}

void GranularGpu_SetTornadoFunnel(Vector3 basePos, float baseRadius, float topRadius,
                                  float height, float swirlSpeed, float updraftSpeed,
                                  bool active)
{
    if (active) {
        s_funnelActive = 1;
        s_funnelSpinDown = 0.0f;
        s_funnelSpinDownTimer = 0.0f;
        if (s_funnelHasPrevBase) {
            float dtEst = GetFrameTime();
            if (dtEst < 0.001f || dtEst > 0.1f) dtEst = 1.0f / 60.0f;
            Vector3 v = Vector3Scale(Vector3Subtract(basePos, s_funnelPrevBase), 1.0f / dtEst);
            float spd = Vector3Length(v);
            if (spd > 25.0f) v = Vector3Scale(v, 25.0f / spd);
            s_funnelVelocity = Vector3Lerp(s_funnelVelocity, v, 0.45f);
        } else {
            s_funnelVelocity = (Vector3){0};
            s_funnelHasPrevBase = true;
        }
        s_funnelPrevBase = basePos;
        s_funnelBase = basePos;
        s_funnelBaseRadius = baseRadius;
        s_funnelTopRadius = topRadius;
        s_funnelHeight = height;
        s_funnelSwirlSpeed = swirlSpeed;
        s_funnelUpdraftSpeed = updraftSpeed;
        s_receiverPoint = basePos;
    } else {
        if (s_funnelActive && s_funnelSpinDownTimer <= 0.0f) {
            // Enter 0.55s spin-down collapse instead of cutting centripetal force abruptly
            s_funnelSpinDownTimer = 0.55f;
            s_funnelSpinDown = 0.0f;
            s_funnelVelocity = Vector3Scale(s_funnelVelocity, 0.35f);
        }
        s_funnelHasPrevBase = false;
    }
}

void GranularGpu_Update(float dt)
{
    if (!s_available || s_grainCount <= 0 || dt <= 0.0f) return;
    s_maxLifeRemaining -= dt;
    if (s_maxLifeRemaining <= 0.0f) {
        s_grainCount = 0;
        return;
    }

    if (s_funnelSpinDownTimer > 0.0f) {
        s_funnelSpinDownTimer -= dt;
        s_funnelSpinDown = Clamp(1.0f - (s_funnelSpinDownTimer / 0.55f), 0.0f, 1.0f);
        s_funnelVelocity = Vector3Scale(s_funnelVelocity, expf(-5.0f * dt));
        if (s_funnelSpinDownTimer <= 0.0f) {
            s_funnelActive = 0;
            s_funnelSpinDown = 0.0f;
            s_funnelSpinDownTimer = 0.0f;
            s_funnelVelocity = (Vector3){0};
        }
    }

    s_elapsedTime += dt;
    float subDt = fminf(dt, 1.0f / 30.0f) * 0.5f;
    unsigned int groups = (unsigned int)((s_grainCount + 255) / 256);
    int cells = GRANULAR_GPU_GRID_CELLS;

    MotionFields_PackGpu(&s_motionScene);
    unsigned int sceneBytes = (unsigned int)(offsetof(MotionGpuScene, fields) +
                                             s_motionScene.meta[0] * sizeof(MotionGpuField));
    rlUpdateShaderBuffer(s_motionSceneSsbo, &s_motionScene, sceneBytes, 0);
    if (s_waveCount > 0) {
        rlUpdateShaderBuffer(s_waveSsbo, s_waveUpload, (unsigned int)(sizeof(GranularGpuWave) * s_waveCount), 0);
    }

    if (s_gridStamp > GRANULAR_STAMP_WRAP) GranularGpu_ClearGrid();

    rlEnableShader(s_program);
    rlBindShaderBuffer(s_waveSsbo, 4);
    rlBindShaderBuffer(s_motionSceneSsbo, 5);
    rlBindShaderBuffer(s_motionBodySsbo, 6);
    rlSetUniform(s_compUni.gridCellCount, &cells, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_compUni.grainCount, &s_grainCount, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_compUni.dt, &subDt, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.time, &s_elapsedTime, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.cellSize, &s_cellSize, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.receiverPoint, &s_receiverPoint.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.receiverNormal, &s_receiverNormal.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.gridOrigin, &s_receiverPoint.x, RL_SHADER_UNIFORM_VEC3, 1);

    rlSetUniform(s_compUni.springStiffness, &s_activeMat.springStiffness, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.dashpotDamping, &s_activeMat.dashpotDamping, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.shearFrictionMu, &s_activeMat.shearFrictionMu, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.rollingResistance, &s_activeMat.rollingResistance, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.cohesion, &s_activeMat.cohesion, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.airDrag, &s_activeMat.airDrag, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.saltationSplash, &s_activeMat.saltationSplash, RL_SHADER_UNIFORM_FLOAT, 1);

    rlSetUniform(s_compUni.impulseCenter, &s_impulseCenter.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.impulseRadius, &s_impulseRadius, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.impulseAccel, &s_impulseAccel.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.impulseSwirl, &s_impulseSwirl, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelActive, &s_funnelActive, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(s_compUni.funnelBase, &s_funnelBase.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.funnelVelocity, &s_funnelVelocity.x, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(s_compUni.funnelSpinDown, &s_funnelSpinDown, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelBaseRadius, &s_funnelBaseRadius, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelTopRadius, &s_funnelTopRadius, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelHeight, &s_funnelHeight, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelSwirlSpeed, &s_funnelSwirlSpeed, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.funnelUpdraftSpeed, &s_funnelUpdraftSpeed, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(s_compUni.waveCount, &s_waveCount, RL_SHADER_UNIFORM_INT, 1);

    for (int sub = 0; sub < 2; sub++) {
        // Phase 1: integrate + insert into grid
        s_gridStamp++;
        int phase = 1;
        rlBindShaderBuffer(s_stateA, 0);
        rlBindShaderBuffer(s_stateB, 1);
        rlBindShaderBuffer(s_heads, 2);
        rlBindShaderBuffer(s_next, 3);
        rlSetUniform(s_compUni.gridStamp, &s_gridStamp, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniform(s_compUni.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
        rlComputeShaderDispatch(groups, 1, 1);

        unsigned int tmp = s_stateA; s_stateA = s_stateB; s_stateB = tmp;

        // Phase 2: DEM 27-cell neighbor solve
        phase = 2;
        rlBindShaderBuffer(s_stateA, 0);
        rlBindShaderBuffer(s_stateB, 1);
        rlSetUniform(s_compUni.phase, &phase, RL_SHADER_UNIFORM_INT, 1);
        rlComputeShaderDispatch(groups, 1, 1);

        tmp = s_stateA; s_stateA = s_stateB; s_stateB = tmp;
    }

    rlDisableShader();
    s_impulseRadius = 0.0f;
}

void GranularGpu_DrawBody(Camera3D camera, const GranularMaterialProfile *mat)
{
    if (!s_available || s_grainCount <= 0) return;
    const GranularMaterialProfile *m = mat ? mat : &s_activeMat;

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

    float baseCol[3] = {m->baseColor.r / 255.0f, m->baseColor.g / 255.0f, m->baseColor.b / 255.0f};
    float shadCol[3] = {m->shadowColor.r / 255.0f, m->shadowColor.g / 255.0f, m->shadowColor.b / 255.0f};
    float highCol[3] = {m->highlightColor.r / 255.0f, m->highlightColor.g / 255.0f, m->highlightColor.b / 255.0f};
    float lightDir[3] = {0.42f, 0.82f, 0.39f};

    BeginShaderMode(s_renderShader);
    if (s_drawUni.view >= 0) SetShaderValueMatrix(s_renderShader, s_drawUni.view, view);
    if (s_drawUni.projection >= 0) SetShaderValueMatrix(s_renderShader, s_drawUni.projection, projection);
    if (s_drawUni.aspectRatio >= 0) SetShaderValue(s_renderShader, s_drawUni.aspectRatio, &m->grainAspect, SHADER_UNIFORM_FLOAT);
    if (s_drawUni.time >= 0) SetShaderValue(s_renderShader, s_drawUni.time, &s_elapsedTime, SHADER_UNIFORM_FLOAT);
    if (s_drawUni.baseColor >= 0) SetShaderValue(s_renderShader, s_drawUni.baseColor, baseCol, SHADER_UNIFORM_VEC3);
    if (s_drawUni.shadowColor >= 0) SetShaderValue(s_renderShader, s_drawUni.shadowColor, shadCol, SHADER_UNIFORM_VEC3);
    if (s_drawUni.highlightColor >= 0) SetShaderValue(s_renderShader, s_drawUni.highlightColor, highCol, SHADER_UNIFORM_VEC3);
    if (s_drawUni.glintIntensity >= 0) SetShaderValue(s_renderShader, s_drawUni.glintIntensity, &m->glintIntensity, SHADER_UNIFORM_FLOAT);
    if (s_drawUni.glintSharpness >= 0) SetShaderValue(s_renderShader, s_drawUni.glintSharpness, &m->glintSharpness, SHADER_UNIFORM_FLOAT);
    if (s_drawUni.lightDir >= 0) SetShaderValue(s_renderShader, s_drawUni.lightDir, lightDir, SHADER_UNIFORM_VEC3);
    if (s_drawUni.camPos >= 0) SetShaderValue(s_renderShader, s_drawUni.camPos, &camera.position.x, SHADER_UNIFORM_VEC3);

    rlBindShaderBuffer(s_stateA, 0);
    rlEnableShader(s_renderShader.id);
    rlEnableVertexArray(s_vao);
    rlDrawVertexArrayInstanced(0, 36, s_grainCount);
    rlDisableVertexArray();
    rlDisableShader();
    EndShaderMode();
}
