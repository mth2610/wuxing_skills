#include "core/emitter/particle_children.h"
#include "particle_gpu_legacy.h"
#include "core/emitter/emitter_gpu.h"
#include "particle_gpu_work_gate.h"
#include "particle_field_lease.h"
#include "core/motion/motion_gpu.h"
#include "core/resource_manager.h"
#include "core/shading/shader_preprocessor.h"
#include "core/particles/particle_system.h"
#include "core/wind/wind_system.h"
#include "core/scene_targets.h"
#include "core/screen_distort.h"
#if defined(GRAPHICS_API_VULKAN) || defined(WUXING_USE_VULKAN)
#include "third_party/vulkan/rlvk.h"
#else
#include "rlgl.h"
#endif
#include "raymath.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// GPU particle data — phải khớp struct trong .comp và _ssbo.vs
// ---------------------------------------------------------------------------
typedef struct
{
    float px, py, pz, radius;
    float vx, vy, vz, drag;
    float csr, csg, csb, csa;
    float cer, ceg, ceb, cea;
    float life_rem, life_max, phase, active;
    float ff_index, ff_pad0, ff_pad1, ff_pad2; // ff_index: slot vào ForceFieldBuffer, -1 = none
    float emitter_id, render_mode, route_pad0, route_pad1;
    float impact_age, impact_active, wind_influence, impact_pad1;
    float formation_x, formation_y, formation_z, formation_pad;
} GpuParticleData;

typedef char GpuParticleDataStrideAssert[(sizeof(GpuParticleData) == GPU_PARTICLE_DATA_STRIDE_BYTES) ? 1 : -1];

#define MAX_GPU_TRAVEL_PATHS 32
#define MAX_GPU_IMPACT_EFFECTS 32

/* std430 mirror of ParticleTravelPathGPU in particle_gpu.comp. All paths are
 * shared: 8192 particles store only a slot + waypoint, never waypoint arrays. */
typedef struct
{
    Vector4 points[PARTICLE_TRAVEL_MAX_WAYPOINTS];
    Vector4 params;  /* x=count, y=speed, z=steering, w=max acceleration */
    Vector4 radii;   /* x=waypoint radius, y=target radius, z=arrivalForceDuration */
    Vector4 arrival; /* x=arrival force-field slot, y=offset, z=kick, w=velScale */
    Vector4 formation_origin;
} ParticleTravelPathGPU;

// ---------------------------------------------------------------------------
// Cached Shader Uniform Locations — tránh tra cứu chuỗi (strcmp) mỗi frame
// trên Vulkan/OpenGL backend.
// ---------------------------------------------------------------------------
typedef struct
{
    int filterBlend;
    int blendLaw;
    int surfaceIndexed;
    int materialId;
    int matchFrontMaterial;
    int frontDepthTex;
    int right;
    int up;
    int mvp;
    int view;
    int projection;
    int filterEmitter;
    int filterMode;
    int softFade;
} GpuDrawShaderLocs;

static void CacheDrawShaderLocs(Shader sh, GpuDrawShaderLocs *locs)
{
    if (!locs) return;
    if (sh.id == 0) {
        memset(locs, -1, sizeof(*locs));
        return;
    }
    locs->filterBlend        = GetShaderLocation(sh, "u_filterBlend");
    locs->blendLaw           = GetShaderLocation(sh, "u_blendLaw");
    locs->surfaceIndexed     = GetShaderLocation(sh, "u_surfaceIndexed");
    locs->materialId         = GetShaderLocation(sh, "u_materialId");
    locs->matchFrontMaterial = GetShaderLocation(sh, "u_matchFrontMaterial");
    locs->frontDepthTex      = GetShaderLocation(sh, "u_frontDepthTex");
    locs->right              = GetShaderLocation(sh, "u_right");
    locs->up                 = GetShaderLocation(sh, "u_up");
    locs->mvp                = GetShaderLocation(sh, "mvp");
    locs->view               = GetShaderLocation(sh, "u_view");
    locs->projection         = GetShaderLocation(sh, "u_projection");
    locs->filterEmitter      = GetShaderLocation(sh, "u_filterEmitter");
    locs->filterMode         = GetShaderLocation(sh, "u_filterRenderMode");
    locs->softFade           = GetShaderLocation(sh, "u_softFade");
}

// ---------------------------------------------------------------------------
// Force field registry — map con trỏ ForceField (CPU) -> slot GPU.
// Compute packs fields into binding 1; CPU/VBO evaluates the same pointers.
// ---------------------------------------------------------------------------
static GpuParticleFieldLease s_fieldLeases[MAX_GPU_FORCE_FIELDS];

static inline const ForceField *FieldAt(int slot)
{
    if (slot < 0 || slot >= MAX_GPU_FORCE_FIELDS) return NULL;
    return (const ForceField *)s_fieldLeases[slot].field;
}

static Vector3 s_fieldAxisOrigin[MAX_GPU_FORCE_FIELDS];
static Vector3 s_fieldAxisDir[MAX_GPU_FORCE_FIELDS];
static int s_fieldCount = 0;

static int RegisterField(const ForceField *ff, Vector3 axisOrigin, Vector3 axisDir,
                         float remainingLife)
{
    int idx = GpuParticleFieldLease_Register(s_fieldLeases, MAX_GPU_FORCE_FIELDS,
                                            ff, remainingLife);
    if (idx < 0) {
        if (ff && remainingLife > 0.0f)
            TraceLog(LOG_WARNING, "GPU_PARTICLES: force field registry full (%d), ignoring", MAX_GPU_FORCE_FIELDS);
        return -1;
    }
    s_fieldAxisOrigin[idx] = axisOrigin;
    s_fieldAxisDir[idx] = axisDir;
    if (idx >= s_fieldCount) s_fieldCount = idx + 1;
    return idx;
}

static const ParticleTravelPath *s_pathRegistry[MAX_GPU_TRAVEL_PATHS];
static int s_pathRefs[MAX_GPU_TRAVEL_PATHS];
static int s_activePathSlots;
static EmissionChildrenHandle s_particleImpactIndex[MAX_GPU_PARTICLES];
static int s_particleImpactCount[MAX_GPU_PARTICLES];

static int RegisterPath(const ParticleTravelPath *path)
{
    int freeSlot = -1;
    if (!path || ParticleTravel_WaypointCount(path) <= 0) return -1;
    for (int i = 0; i < MAX_GPU_TRAVEL_PATHS; ++i) {
        if (s_pathRegistry[i] == path) {
            s_pathRefs[i]++;
            return i;
        }
        if (freeSlot < 0 && s_pathRefs[i] == 0) freeSlot = i;
    }
    if (freeSlot < 0) {
        TraceLog(LOG_WARNING, "GPU_PARTICLES: travel path registry full (%d), ignoring",
                 MAX_GPU_TRAVEL_PATHS);
        return -1;
    }
    s_pathRegistry[freeSlot] = path;
    s_pathRefs[freeSlot] = 1;
    s_activePathSlots++;
    return freeSlot;
}

static void ReleasePath(int slot)
{
    if (slot < 0 || slot >= MAX_GPU_TRAVEL_PATHS || s_pathRefs[slot] <= 0) return;
    if (--s_pathRefs[slot] == 0) {
        s_pathRegistry[slot] = NULL;
        if (s_activePathSlots > 0) s_activePathSlots--;
    }
}

static void PackTravelPath(const ParticleTravelPath *path,
                           ParticleTravelPathGPU *packed)
{
    int count = ParticleTravel_WaypointCount(path);
    if (count > PARTICLE_TRAVEL_MAX_WAYPOINTS)
        count = PARTICLE_TRAVEL_MAX_WAYPOINTS;
    memset(packed, 0, sizeof(*packed));
    for (int i = 0; i < count; ++i) {
        Vector3 point = ParticleTravel_GetWaypoint(path, i);
        packed->points[i] = (Vector4){point.x, point.y, point.z, 0.0f};
    }
    packed->params = (Vector4){(float)count, path->speed, path->steering,
                               path->maxAcceleration};
    packed->radii = (Vector4){path->waypointRadius, path->targetRadius,
                              path->arrivalForceDuration, 0.0f};
    packed->arrival = (Vector4){
        (float)RegisterField(path->arrivalForceField, (Vector3){0}, (Vector3){0}, 0.0f),
        path->arrivalOffset, path->arrivalKick, path->arrivalVelocityScale};
    if (path->formationOrigin) {
        packed->formation_origin = (Vector4){path->formationOrigin->x,
                                             path->formationOrigin->y,
                                             path->formationOrigin->z, 0.0f};
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
#include "core/motion/motion_wind_gpu.h"

static bool s_initialized = false;
static bool s_use_compute = false;

static unsigned int s_surface_index_ssbo = 0;
static GpuSurfaceIndex s_surfaceIndices[MAX_GPU_PARTICLES];
/* Owner/mode of uploaded slots, not pending CPU event-spawn replacements. */
static GpuSurfaceRouting s_surfaceResidentRouting[MAX_GPU_PARTICLES];
static GpuSurfaceRoute s_surfaceRoutes[GPU_SURFACE_MAX_STREAMS];
static unsigned int s_surfaceRevision, s_surfaceCachedRevision;
static int s_surfaceRouteCount, s_surfaceIndexCount;
static bool s_surfaceCacheValid, s_surfaceIndexed;
static int s_surfaceLastInstanceCount;
static unsigned int s_ssbo = 0;
static unsigned int s_ff_ssbo = 0;           // ForceFieldBuffer, binding = 1
static unsigned int s_path_ssbo = 0;         // ParticleTravelPathBuffer, binding = 2
static unsigned int s_wind_ssbo = 0;         // WindBuffer, binding = 3
static unsigned int s_wind_terrain_ssbo = 0; // WindTerrainBuffer, binding = 4
static unsigned int s_motion_scene_ssbo = 0, s_motion_body_ssbo = 0;
static MotionGpuScene s_motionScene;
static MotionGpuBody s_motionBodies[MAX_GPU_PARTICLES];
static int s_motionLiveCount = 0;
static bool s_drawingChildren = false;
static int s_drawBlend = VFX_BLEND_ADDITIVE;
static unsigned int s_blendMask = 0;
static unsigned int s_uploaded_wind_terrain_version = ~0u;
static unsigned int s_compute_prog = 0;
static int s_comp_loc_dt = -1;
static int s_comp_loc_time = -1;
static int s_comp_loc_vecField[GPU_VECTOR_FIELD_SLOTS];

static unsigned int s_draw_vao = 0;
static unsigned int s_draw_quad_vbo = 0; // template quad, attribute 0
static Shader s_draw_shader_gpu = {0};
static GpuDrawShaderLocs s_draw_locs_gpu;

/* Liquid-table slot the NEXT surface draw rasterizes into the capture's B
 * channel. Set by core/liquid/liquid_surface.c before each stream so several
 * liquids can share one capture; see LiquidDesc in liquid_surface.h. */
static float s_surfaceMaterialId = 0.0f;
static Texture2D s_surfaceFrontDepth = {0};
static Shader s_surface_capture_shader_gpu = {0};
static GpuDrawShaderLocs s_surface_capture_locs_gpu;
static Shader s_surface_capture_shader_cpu = {0};
static int s_cpu_capture_material_loc = -1;
static Shader s_surface_back_shader_gpu = {0};
static GpuDrawShaderLocs s_surface_back_locs_gpu;
static Shader s_surface_back_shader_cpu = {0};
static int s_cpu_capture_params_loc = -1;
static int s_cpu_capture_proj_loc = -1;
static int s_cpu_back_params_loc = -1;
static int s_cpu_back_proj_loc = -1;
static float s_elapsed_time = 0.0f;

static GpuParticleData s_cpu_pool[MAX_GPU_PARTICLES];

static int s_spawn_cursor = 0;
static bool s_hasSpawned = false;
static int s_spawn_start_this_frame = -1;
static int s_spawn_count_this_frame = 0;
static int s_filterEmitter = -1, s_filterRenderMode = -1;
static int s_surfacePass = 0; /* 0 normal, 1 front depth, 3 back depth */

// Vector field textures cho FORCE_VECTOR_TEXTURE — không sở hữu (không Unload
// ở đây), chỉ bind vào texture unit trước mỗi dispatch khi slot đang set.
static Texture2D s_vectorFieldTex[GPU_VECTOR_FIELD_SLOTS] = {0};

// Matches the CPU particle default. GPU billboards sample the prior frame's
// linear scene-depth snapshot, so their intersection with terrain fades rather
// than being clipped by a hard depth-test rail.
#define GPU_PARTICLE_SOFT_DEPTH_SLOT 3
#define GPU_PARTICLE_SOFT_FADE_METERS 0.35f

static inline unsigned char ClampFloatToByte(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (unsigned char)(v + 0.5f);
}

static void ComputeCameraBasis(Camera3D camera, Vector3 *outViewDir,
                               Vector3 *outRight, Vector3 *outUp)
{
    Vector3 viewDir = Vector3Subtract(camera.position, camera.target);
    float viewLenSq = Vector3LengthSqr(viewDir);
    if (viewLenSq > 1e-8f)
        viewDir = Vector3Scale(viewDir, 1.0f / sqrtf(viewLenSq));
    else
        viewDir = (Vector3){0.0f, 0.0f, 1.0f};

    Vector3 right = Vector3CrossProduct(camera.up, viewDir);
    float rightLenSq = Vector3LengthSqr(right);
    if (rightLenSq < 1e-8f) {
        right = Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, viewDir);
        rightLenSq = Vector3LengthSqr(right);
        if (rightLenSq < 1e-8f) {
            right = Vector3CrossProduct((Vector3){1.0f, 0.0f, 0.0f}, viewDir);
            rightLenSq = Vector3LengthSqr(right);
        }
    }
    if (rightLenSq > 1e-8f)
        right = Vector3Scale(right, 1.0f / sqrtf(rightLenSq));
    else
        right = (Vector3){1.0f, 0.0f, 0.0f};

    Vector3 up = Vector3CrossProduct(viewDir, right);
    if (outViewDir) *outViewDir = viewDir;
    if (outRight) *outRight = right;
    if (outUp) *outUp = up;
}

// ---------------------------------------------------------------------------
// Compute shader loader
// Source dùng #version 310 es (GLES 3.1).
// Desktop GL 4.3: runtime patch lên #version 430 core.
// ---------------------------------------------------------------------------
static unsigned int CompileComputeShader(const char *path)
{
    char *src = ShaderPreprocessor_Load(path);
    if (!src)
    {
        TraceLog(LOG_WARNING, "GPU_PARTICLES: cannot load %s", path);
        return 0;
    }

    char *patched = NULL;
#if !defined(__ANDROID__)
    {
        const char *from = "#version 310 es";
        const char *to = "#version 430 core";
        char *hit = strstr(src, from);
        if (hit)
        {
            int from_len = (int)strlen(from);
            int to_len = (int)strlen(to);
            int src_len = (int)strlen(src);
            int prefix = (int)(hit - src);
            int suffix = src_len - prefix - from_len;
            patched = (char *)RL_MALLOC(src_len - from_len + to_len + 1);
            memcpy(patched, src, prefix);
            memcpy(patched + prefix, to, to_len);
            memcpy(patched + prefix + to_len, hit + from_len, suffix + 1);
        }
    }
#endif
    const char *final_src = patched ? patched : src;

    unsigned int compShader = rlLoadShader(final_src, RL_COMPUTE_SHADER);
    if (patched)
        RL_FREE(patched);
    RL_FREE(src);

    if (compShader == 0)
    {
        TraceLog(LOG_ERROR, "GPU_PARTICLES compute compile failed");
        return 0;
    }

    unsigned int prog = rlLoadShaderProgramCompute(compShader);
    rlUnloadShader(compShader);

    if (prog == 0)
    {
        TraceLog(LOG_ERROR, "GPU_PARTICLES compute link failed");
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void GpuParticleSystem_Init(void)
{
    if (s_initialized)
        return;

    memset(s_cpu_pool, 0, sizeof(s_cpu_pool));
    memset(s_motionBodies, 0, sizeof(s_motionBodies));
    s_motionLiveCount = 0;
    s_blendMask = 0;
    s_drawingChildren = false;
    memset(s_vectorFieldTex, 0, sizeof(s_vectorFieldTex));
    s_spawn_cursor = 0;
    s_hasSpawned = false;
    s_spawn_start_this_frame = -1;
    s_spawn_count_this_frame = 0;
    s_fieldCount = 0;
    memset(s_fieldLeases, 0, sizeof(s_fieldLeases));
    s_surfaceRevision = 0;
    s_surfaceCachedRevision = 0;
    s_surfaceRouteCount = 0;
    s_surfaceIndexCount = 0;
    s_surfaceCacheValid = s_surfaceIndexed = false;
    s_surfaceLastInstanceCount = 0;
    memset(s_surfaceResidentRouting, 0, sizeof(s_surfaceResidentRouting));
    s_surfaceFrontDepth = (Texture2D){0};
    memset(s_pathRegistry, 0, sizeof(s_pathRegistry));
    memset(s_pathRefs, 0, sizeof(s_pathRefs));
    s_activePathSlots = 0;
    EmissionImpact_Init();
    for (int i = 0; i < MAX_GPU_PARTICLES; ++i) {
        s_particleImpactIndex[i] = 0;
        s_particleImpactCount[i] = 0;
    }
    s_elapsed_time = 0.0f;
    s_comp_loc_dt = -1;
    s_comp_loc_time = -1;
    for (int i = 0; i < GPU_VECTOR_FIELD_SLOTS; ++i)
        s_comp_loc_vecField[i] = -1;
    CacheDrawShaderLocs((Shader){0}, &s_draw_locs_gpu);
    CacheDrawShaderLocs((Shader){0}, &s_surface_capture_locs_gpu);
    CacheDrawShaderLocs((Shader){0}, &s_surface_back_locs_gpu);

#if defined(GRAPHICS_API_VULKAN) || defined(WUXING_USE_VULKAN)
    bool gl43 = true;
#else
    bool gl43 = (rlGetVersion() == 4); // GL 4.3+
#endif

#if defined(__ANDROID__) && !defined(GRAPHICS_API_VULKAN) && !defined(WUXING_USE_VULKAN)
    // Force the CPU/VBO particle path on Android (ANDROID_NOTICES §D) for OpenGL ES.
    // Vulkan backend explicitly supports and mandates compute/SSBO functionality.
    gl43 = false;
#endif

    TraceLog(LOG_INFO, "GPU_PARTICLES: rlGetVersion = %d", rlGetVersion());

    const char *forceCpu = getenv("WUXING_PARTICLES_FORCE_CPU");
    if (forceCpu && strcmp(forceCpu, "1") == 0) {
        gl43 = false;
        TraceLog(LOG_WARNING, "GPU_PARTICLES: WUXING_PARTICLES_FORCE_CPU=1 selects CPU simulation and capabilities");
    }

    if (gl43)
    {
        // ----- COMPUTE PATH -----
        const char *comp_path = "core/particles/shaders/gpu/particle_gpu.comp";
        const char *ssbo_vs_path = "core/particles/shaders/gpu/particle_gpu_ssbo.vs";
        const char *fs_path = "core/particles/shaders/gpu/particle_gpu.fs";

        s_compute_prog = CompileComputeShader(comp_path);
        if (!s_compute_prog)
        {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: compute compile failed, fallback CPU");
            goto cpu_path;
        }

        s_comp_loc_dt = rlGetLocationUniform(s_compute_prog, "u_dt");
        s_comp_loc_time = rlGetLocationUniform(s_compute_prog, "u_time");
        for (int slot = 0; slot < GPU_VECTOR_FIELD_SLOTS; ++slot) {
            char uname[24];
            snprintf(uname, sizeof(uname), "uVectorField%d", slot);
            s_comp_loc_vecField[slot] = rlGetLocationUniform(s_compute_prog, uname);
        }

        s_ssbo = rlLoadShaderBuffer(MAX_GPU_PARTICLES * (ptrdiff_t)sizeof(GpuParticleData), NULL, RL_DYNAMIC_DRAW);
        s_surface_index_ssbo = rlLoadShaderBuffer(MAX_GPU_PARTICLES * (ptrdiff_t)sizeof(GpuSurfaceIndex), NULL, RL_DYNAMIC_DRAW);
        s_ff_ssbo = rlLoadShaderBuffer(MAX_GPU_FORCE_FIELDS * (ptrdiff_t)sizeof(ForceFieldGPU), NULL, RL_DYNAMIC_DRAW);
        s_path_ssbo = rlLoadShaderBuffer(MAX_GPU_TRAVEL_PATHS * (ptrdiff_t)sizeof(ParticleTravelPathGPU), NULL, RL_DYNAMIC_DRAW);
        s_wind_ssbo = rlLoadShaderBuffer((unsigned int)sizeof(WindGPU), NULL, RL_DYNAMIC_DRAW);
        s_wind_terrain_ssbo = rlLoadShaderBuffer((unsigned int)sizeof(WindTerrainGPU), NULL, RL_DYNAMIC_DRAW);
        s_motion_scene_ssbo = rlLoadShaderBuffer(sizeof(MotionGpuScene), NULL, RL_DYNAMIC_DRAW);
        s_motion_body_ssbo = rlLoadShaderBuffer(sizeof(s_motionBodies), s_motionBodies, RL_DYNAMIC_DRAW);
        s_uploaded_wind_terrain_version = ~0u;

        s_draw_shader_gpu = ResourceManager_LoadShader(ssbo_vs_path, fs_path);
        if (s_draw_shader_gpu.id == 0)
        {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: draw shader compile failed, fallback CPU");
            rlDisableShader();
            rlUnloadShaderProgram(s_compute_prog);
            s_compute_prog = 0;
            if (s_surface_index_ssbo) { rlUnloadShaderBuffer(s_surface_index_ssbo); s_surface_index_ssbo = 0; }
            if (s_ssbo) { rlUnloadShaderBuffer(s_ssbo); s_ssbo = 0; }
            if (s_ff_ssbo) { rlUnloadShaderBuffer(s_ff_ssbo); s_ff_ssbo = 0; }
            if (s_path_ssbo) { rlUnloadShaderBuffer(s_path_ssbo); s_path_ssbo = 0; }
            if (s_wind_ssbo) { rlUnloadShaderBuffer(s_wind_ssbo); s_wind_ssbo = 0; }
            if (s_wind_terrain_ssbo) { rlUnloadShaderBuffer(s_wind_terrain_ssbo); s_wind_terrain_ssbo = 0; }
            if (s_motion_scene_ssbo) { rlUnloadShaderBuffer(s_motion_scene_ssbo); s_motion_scene_ssbo = 0; }
            if (s_motion_body_ssbo) { rlUnloadShaderBuffer(s_motion_body_ssbo); s_motion_body_ssbo = 0; }
            goto cpu_path;
        }
        CacheDrawShaderLocs(s_draw_shader_gpu, &s_draw_locs_gpu);

        s_surface_capture_shader_gpu = ResourceManager_LoadShader("core/particles/shaders/gpu/liquid_surface_capture.vs", "core/liquid/shaders/liquid_capture_particle.fs");
        s_surface_back_shader_gpu = ResourceManager_LoadShader("core/particles/shaders/gpu/liquid_surface_capture.vs", "core/liquid/shaders/liquid_capture_particle_back.fs");
        if (s_surface_capture_shader_gpu.id == 0)
        {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: surface capture shader unavailable");
        }
        CacheDrawShaderLocs(s_surface_capture_shader_gpu, &s_surface_capture_locs_gpu);
        CacheDrawShaderLocs(s_surface_back_shader_gpu, &s_surface_back_locs_gpu);

        // Template quad (2 tam giác, góc ±1, CCW = front-face) ở attribute 0.
        // Per-particle data đi qua SSBO binding 0 đọc bằng gl_InstanceID trong VS.
        static const float quadTemplate[18] = {
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f,
        };
        s_draw_vao = rlLoadVertexArray();
        rlEnableVertexArray(s_draw_vao);
        s_draw_quad_vbo = rlLoadVertexBuffer(quadTemplate, sizeof(quadTemplate), false);
        rlSetVertexAttribute(0, 3, RL_FLOAT, 0, 0, 0);
        rlEnableVertexAttribute(0);
        rlDisableVertexArray();

        s_use_compute = true;
        TraceLog(LOG_INFO, "GPU_PARTICLES: COMPUTE path active (%d particles)", MAX_GPU_PARTICLES);
    }
    else
    {
cpu_path:
        // Sphere impostor shaders for CPU SSF capture/thickness.
        s_surface_capture_shader_cpu = ResourceManager_LoadShader(NULL, "core/liquid/shaders/liquid_capture_cpu.fs");
        s_surface_back_shader_cpu = ResourceManager_LoadShader(NULL, "core/liquid/shaders/liquid_capture_cpu_back.fs");
        s_cpu_capture_params_loc   = GetShaderLocation(s_surface_capture_shader_cpu, "u_capture_params");
        s_cpu_capture_material_loc = GetShaderLocation(s_surface_capture_shader_cpu, "u_materialId");
        s_cpu_capture_proj_loc     = GetShaderLocation(s_surface_capture_shader_cpu, "u_projection");
        s_cpu_back_params_loc      = GetShaderLocation(s_surface_back_shader_cpu, "u_capture_params");
        s_cpu_back_proj_loc        = GetShaderLocation(s_surface_back_shader_cpu, "u_projection");
        // ----- CPU/VBO PATH -----
        s_use_compute = false;
        TraceLog(LOG_INFO, "GPU_PARTICLES: CPU/VBO path active (%d particles)", MAX_GPU_PARTICLES);
    }

    s_initialized = true;
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------
bool GpuParticleSystem_TrySpawn(GpuParticleConfig cfg)
{
    if (!s_initialized)
        return false;
    int idx = s_spawn_cursor % MAX_GPU_PARTICLES;
    if (s_use_compute && !EmissionGpu_SetParent((unsigned int)idx,
            cfg.emissionConfig, cfg.emitterId)) {
        return false;
    }
    s_hasSpawned = true;
    ++s_surfaceRevision;
    s_spawn_cursor++;

    if (s_cpu_pool[idx].active >= 0.5f) {
        if (s_motionBodies[idx].meta[0]) --s_motionLiveCount;
        ReleasePath((int)s_cpu_pool[idx].route_pad0);
        EmissionImpact_Release(s_particleImpactIndex[idx]);
        s_particleImpactIndex[idx] = 0;
        s_particleImpactCount[idx] = 0;
    }

    GpuParticleData d;
    d.px = cfg.position.x;
    d.py = cfg.position.y;
    d.pz = cfg.position.z;
    d.radius = cfg.radius;
    d.vx = cfg.velocity.x;
    d.vy = cfg.velocity.y;
    d.vz = cfg.velocity.z;
    d.drag = cfg.drag;
    float boost = cfg.emissiveBoost > 0.0f ? cfg.emissiveBoost : 1.0f;
    /* GPU particles use the shared structured default sprite. Its RGB is the
       authored white-hot core/orange rim, so config colour contributes alpha
       over lifetime only; multiplying cyan/yellow tint into it would erase the
       material hierarchy. */
    d.csr = boost;
    d.csg = boost;
    d.csb = boost;
    d.csa = cfg.colorStart.a * (1.0f / 255.0f);
    d.cer = boost;
    d.ceg = boost;
    d.ceb = boost;
    d.cea = cfg.colorEnd.a * (1.0f / 255.0f);
    float safeLife = cfg.lifetime > 1e-6f ? cfg.lifetime : 1e-6f;
    d.life_rem = safeLife;
    d.life_max = safeLife;
    d.phase = (float)GetRandomValue(0, 10000) * 0.0001f;
    d.active = 1.0f;
    d.ff_index = (float)RegisterField(cfg.forceField, cfg.axisOrigin, cfg.axisDir, safeLife);
    d.ff_pad0 = cfg.stretchStrength;
    d.ff_pad1 = cfg.collisionEnabled ? cfg.collisionElasticity : -1.0f;
    d.ff_pad2 = cfg.collisionFloorY;
    d.emitter_id = (float)cfg.emitterId;
    d.render_mode = (float)cfg.renderMode;
    if (cfg.travelPath)
        (void)RegisterField(cfg.travelPath->arrivalForceField,
                            (Vector3){0}, (Vector3){0}, safeLife);
    d.route_pad0 = (float)RegisterPath(cfg.travelPath);
    d.route_pad1 = 0.0f;
    d.impact_age = 0.0f;
    d.impact_active = 0.0f;
    d.wind_influence = cfg.windInfluence;
    d.impact_pad1 = 0.0f;
    s_motionBodies[idx] = (MotionGpuBody){0};
    if (cfg.spatialMotionOnly) {
        s_motionBodies[idx] = MotionGpu_PackBody(cfg.dynamics, cfg.initialAccelerationMps2,
            cfg.constantForceNewtons, cfg.receiveMotionFields, cfg.windInfluence);
        MotionGpuBody *body = &s_motionBodies[idx];
        if (cfg.dynamics && cfg.drag > 0.0f && body->body0.z <= 0.0f)
            body->body0.z = cfg.drag;
        d.vx += cfg.initialImpulseNs.x * body->body0.x;
        d.vy += cfg.initialImpulseNs.y * body->body0.x;
        d.vz += cfg.initialImpulseNs.z * body->body0.x;
        d.impact_pad1 = (float)cfg.blendMode;
        ++s_motionLiveCount;
    } else {
        d.impact_pad1 = (float)VFX_BLEND_ADDITIVE;
    }
    int blendBit = (int)d.impact_pad1;
    if (blendBit >= 0 && blendBit < 32)
        s_blendMask |= (1u << blendBit);

    d.formation_x = d.formation_y = d.formation_z = d.formation_pad = 0.0f;
    if (cfg.travelPath && cfg.travelPath->formationOrigin) {
        d.formation_x = cfg.position.x - cfg.travelPath->formationOrigin->x;
        d.formation_y = cfg.position.y - cfg.travelPath->formationOrigin->y;
        d.formation_z = cfg.position.z - cfg.travelPath->formationOrigin->z;
    }
    s_particleImpactIndex[idx] = cfg.onTargetEmitCount > 0
                                     ? EmissionImpact_Retain(cfg.onTargetEmit) : 0;
    s_particleImpactCount[idx] = cfg.onTargetEmitCount > 0 ? cfg.onTargetEmitCount : 0;

    s_cpu_pool[idx] = d;

    if (s_use_compute)
    {
        if (s_spawn_start_this_frame == -1)
            s_spawn_start_this_frame = idx;
        s_spawn_count_this_frame++;
    }
    return true;
}

void GpuParticleSystem_Spawn(GpuParticleConfig cfg)
{
    (void)GpuParticleSystem_TrySpawn(cfg);
}

// ---------------------------------------------------------------------------
// Vector field texture registration
// ---------------------------------------------------------------------------
void GpuParticleSystem_SetVectorFieldTexture(int slot, Texture2D tex)
{
    if (slot < 0 || slot >= GPU_VECTOR_FIELD_SLOTS)
        return;
    s_vectorFieldTex[slot] = tex;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
static int LegacyDustRandom(int min, int max, void *user)
{
    (void)user;
    return GetRandomValue(min, max);
}

void GpuParticleSystem_Update(float dt)
{
    if (!GpuParticleWork_BeginUpdate(s_initialized, s_use_compute,
                                     s_hasSpawned, dt,
                                     &s_elapsed_time))
        return;

    GpuParticleFieldLease_Advance(s_fieldLeases, MAX_GPU_FORCE_FIELDS, dt);
    s_fieldCount = GpuParticleFieldLease_ActiveSpan(s_fieldLeases, s_fieldCount);

    if (s_use_compute)
    {
        // Batch upload cho các hạt mới spawn trong frame này
        if (s_spawn_count_this_frame > 0)
        {
            int start = s_spawn_start_this_frame;
            int count = s_spawn_count_this_frame;
            if (count >= MAX_GPU_PARTICLES)
            {
                start = 0;
                count = MAX_GPU_PARTICLES;
            }
            int end = start + count;

            if (end <= MAX_GPU_PARTICLES)
            {
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[start], count * sizeof(GpuParticleData), start * sizeof(GpuParticleData));
                rlUpdateShaderBuffer(s_motion_body_ssbo, &s_motionBodies[start], count * sizeof(MotionGpuBody), start * sizeof(MotionGpuBody));
            }
            else
            {
                int chunk1 = MAX_GPU_PARTICLES - start;
                int chunk2 = count - chunk1;
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[start], chunk1 * sizeof(GpuParticleData), start * sizeof(GpuParticleData));
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[0], chunk2 * sizeof(GpuParticleData), 0);
                rlUpdateShaderBuffer(s_motion_body_ssbo, &s_motionBodies[start], chunk1 * sizeof(MotionGpuBody), start * sizeof(MotionGpuBody));
                rlUpdateShaderBuffer(s_motion_body_ssbo, &s_motionBodies[0], chunk2 * sizeof(MotionGpuBody), 0);
            }
            for (int i = 0; i < count; ++i) {
                int slot = (start + i) % MAX_GPU_PARTICLES;
                s_surfaceResidentRouting[slot] = GpuSurfaceRouting_Read(&s_cpu_pool[slot],
                                                       offsetof(GpuParticleData, emitter_id),
                                                       offsetof(GpuParticleData, render_mode));
            }
            /* Chỉ tăng s_surfaceRevision khi có dữ liệu routing mới được nạp lên GPU,
             * giúp giữ nguyên cache s_surfaceIndices ở các frame không spawn thêm hạt. */
            ++s_surfaceRevision;
            s_spawn_start_this_frame = -1;
            s_spawn_count_this_frame = 0;
        }

        if (s_motionLiveCount > 0 || EmissionGpu_HasChildren()) {
            MotionFields_PackGpu(&s_motionScene);
            rlUpdateShaderBuffer(s_motion_scene_ssbo, &s_motionScene,
                offsetof(MotionGpuScene, fields) + s_motionScene.meta[0] * sizeof(MotionGpuField), 0);
        }
        // Re-pack mọi force field đang hoạt động
        if (s_fieldCount > 0)
        {
            ForceFieldGPU packed[MAX_GPU_FORCE_FIELDS];
            for (int i = 0; i < s_fieldCount; i++)
            {
                const ForceField *ff = FieldAt(i);
                if (ff)
                    ForceField_PackGPU(ff, s_fieldAxisOrigin[i],
                                       s_fieldAxisDir[i], &packed[i]);
                else
                    memset(&packed[i], 0, sizeof(packed[i]));
            }
            rlUpdateShaderBuffer(s_ff_ssbo, packed, s_fieldCount * (ptrdiff_t)sizeof(ForceFieldGPU), 0);
        }

        if (s_activePathSlots > 0) {
            ParticleTravelPathGPU packed[MAX_GPU_TRAVEL_PATHS];
            int highestSlot = -1;
            memset(packed, 0, sizeof(packed));
            for (int i = 0; i < MAX_GPU_TRAVEL_PATHS; ++i) {
                if (s_pathRegistry[i] && s_pathRefs[i] > 0) {
                    PackTravelPath(s_pathRegistry[i], &packed[i]);
                    highestSlot = i;
                }
            }
            if (highestSlot >= 0) {
                rlUpdateShaderBuffer(s_path_ssbo, packed,
                                     (highestSlot + 1) * (ptrdiff_t)sizeof(packed[0]), 0);
            }
        }

        rlEnableShader(s_compute_prog);
        if (s_comp_loc_dt >= 0)
            rlSetUniform(s_comp_loc_dt, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
        if (s_comp_loc_time >= 0)
            rlSetUniform(s_comp_loc_time, &s_elapsed_time, RL_SHADER_UNIFORM_FLOAT, 1);

        for (int slot = 0; slot < GPU_VECTOR_FIELD_SLOTS; slot++)
        {
            if (s_vectorFieldTex[slot].id == 0)
                continue;
            rlActiveTextureSlot(slot);
            rlEnableTexture(s_vectorFieldTex[slot].id);
            int loc_tex = s_comp_loc_vecField[slot];
            if (loc_tex >= 0)
                rlSetUniform(loc_tex, &slot, RL_SHADER_UNIFORM_INT, 1);
        }

        // Pack Wind System state to GPU SSBO
        WindGPU windData;
        MotionGpu_PackWind(&windData);
        rlUpdateShaderBuffer(s_wind_ssbo, &windData, (unsigned int)sizeof(WindGPU), 0);

        const WindTerrainGrid *terrainGrid = Wind_GetTerrainGrid();
        if (terrainGrid && terrainGrid->version != s_uploaded_wind_terrain_version) {
            WindTerrainGPU terrainData;
            MotionGpu_PackWindTerrain(terrainGrid, &terrainData);
            rlUpdateShaderBuffer(s_wind_terrain_ssbo, &terrainData,
                                 (unsigned int)sizeof(terrainData), 0);
            s_uploaded_wind_terrain_version = terrainGrid->version;
        }

        rlBindShaderBuffer(s_ssbo, 0);
        rlBindShaderBuffer(s_ff_ssbo, 1);
        rlBindShaderBuffer(s_path_ssbo, 2);
        rlBindShaderBuffer(s_wind_ssbo, 3);
        rlBindShaderBuffer(s_wind_terrain_ssbo, 4);
        rlBindShaderBuffer(s_motion_scene_ssbo, 5);
        rlBindShaderBuffer(s_motion_body_ssbo, 6);
        /* Children use the same production Motion integrator. Advance existing
         * children before generating births so new children start at age zero. */
        if (EmissionGpu_HasChildren()) {
            rlBindShaderBuffer(EmissionGpu_ChildBuffer(), 0);
            rlBindShaderBuffer(EmissionGpu_ChildBodyBuffer(), 6);
            rlComputeShaderDispatch((EMISSION_GPU_CHILD_CAPACITY + 255) / 256, 1, 1);
            rlBindShaderBuffer(s_ssbo, 0);
            rlBindShaderBuffer(s_motion_body_ssbo, 6);
        }
        /* Before ring wrap, slots beyond the spawn high-water mark have never
         * existed. Bound work by ownership, never by the CPU event shadow. */
        int activeSpan = GpuParticleSystem_ActiveCount();
        unsigned int groups = (unsigned int)((activeSpan + 255) / 256);
        if (groups > 0)
            rlComputeShaderDispatch(groups, 1, 1);
        rlDisableShader();
        EmissionGpu_Dispatch(s_ssbo, (unsigned int)activeSpan, dt);

        for (int slot = 0; slot < GPU_VECTOR_FIELD_SLOTS; slot++)
        {
            if (s_vectorFieldTex[slot].id == 0)
                continue;
            rlActiveTextureSlot(slot);
            rlDisableTexture();
        }
        rlActiveTextureSlot(0);
    }

    // ALWAYS run CPU update loop for tracking and spawning events (like dust puffs on collision!)
    // Bound by high-water mark s_spawn_cursor on both compute and CPU paths.
    int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
    const float baseWindBlend = 1.0f - expf(-3.5f * dt);

    for (int i = 0; i < slotLimit; i++)
    {
        GpuParticleData *p = &s_cpu_pool[i];
        if (p->active < 0.5f)
            continue;
        p->life_rem -= dt;
        if (p->life_rem <= 0.0f)
        {
            if (s_use_compute) EmissionGpu_ReleaseParent((unsigned int)i);
            if (s_motionBodies[i].meta[0]) {
                --s_motionLiveCount;
                s_motionBodies[i].meta[0] = 0;
            }
            ReleasePath((int)p->route_pad0);
            p->route_pad0 = -1.0f;
            EmissionImpact_Release(s_particleImpactIndex[i]);
            s_particleImpactIndex[i] = 0;
            s_particleImpactCount[i] = 0;
            p->active = 0.0f;
            continue;
        }
        /* Motion GPU state is authoritative. Only lifetime/owner bookkeeping
         * runs on CPU: no per-particle field sampling, integration or readback. */
        if (s_use_compute && s_motionBodies[i].meta[0])
            continue;

        int pathIndex = (int)p->route_pad0;
        const ParticleTravelPath *activePath =
            (pathIndex >= 0 && pathIndex < MAX_GPU_TRAVEL_PATHS) ? s_pathRegistry[pathIndex] : NULL;
        bool impactActive = p->impact_active > 0.5f;
        int forceIndex = (int)p->ff_index;
        if (impactActive) {
            p->impact_age += dt;
            if (activePath && activePath->arrivalForceField &&
                (activePath->arrivalForceDuration <= 0.0f || p->impact_age <= activePath->arrivalForceDuration)) {
                forceIndex = RegisterField(activePath->arrivalForceField,
                                           (Vector3){0}, (Vector3){0}, p->life_rem);
            } else {
                forceIndex = -1;
            }
        }

        // 1. Evaluate Force Field on CPU
        if (forceIndex >= 0 && forceIndex < s_fieldCount)
        {
            const ForceField *ff = FieldAt(forceIndex);
            if (ff)
            {
                Vector3 pos = {p->px, p->py, p->pz};
                Vector3 vel = {p->vx, p->vy, p->vz};
                Vector3 acc = ForceField_Evaluate(ff, pos, vel, s_elapsed_time,
                                                  s_fieldAxisOrigin[forceIndex],
                                                  s_fieldAxisDir[forceIndex]);
                p->vx += acc.x * dt;
                p->vy += acc.y * dt;
                p->vz += acc.z * dt;

                float viscDamp = ForceField_GetViscosityDamping(ff, dt);
                p->vx *= viscDamp;
                p->vy *= viscDamp;
                p->vz *= viscDamp;
            }
        }

        // 2. Drag
        if (p->drag > 0.0f)
        {
            float drag_f = 1.0f - p->drag * dt;
            if (drag_f < 0.0f)
                drag_f = 0.0f;
            p->vx *= drag_f;
            p->vy *= drag_f;
            p->vz *= drag_f;
        }

        // 3. Path steering runs after force/drag, then owns integration.
        bool reachedTarget = false;
        if (!impactActive && activePath) {
            Vector3 position = {p->px, p->py, p->pz};
            Vector3 velocity = {p->vx, p->vy, p->vz};
            int waypoint = (int)p->route_pad1;
            reachedTarget = ParticleTravel_StepFormation(activePath, dt,
                                                &position, &velocity, &waypoint,
                                                (Vector3){p->formation_x, p->formation_y, p->formation_z});
            p->px = position.x; p->py = position.y; p->pz = position.z;
            p->vx = velocity.x; p->vy = velocity.y; p->vz = velocity.z;
            p->route_pad1 = (float)waypoint;
        } else {
            if (p->wind_influence > 0.0f) {
                // Jitter per-particle: dùng floorf thay vì fmodf+branch để khớp GLSL fract() và nhanh hơn
                float seed = (float)i;
                float s1 = sinf(seed * 127.1f) * 43758.5453f;
                float s2 = sinf(seed * 269.5f) * 43758.5453f;
                float s3 = sinf(seed * 419.2f) * 43758.5453f;
                float h1 = s1 - floorf(s1);
                float h2 = s2 - floorf(s2);
                float h3 = s3 - floorf(s3);
                Vector3 samplePos = {
                    p->px + (h1 - 0.5f) * 3.0f,
                    p->py + (h2 - 0.5f) * 3.0f,
                    p->pz + (h3 - 0.5f) * 3.0f
                };
                Vector3 windVel = Wind_EvaluateVelocity(samplePos, s_elapsed_time);
                float blendRate = baseWindBlend * p->wind_influence;
                p->vx += (windVel.x - p->vx) * blendRate;
                p->vy += (windVel.y - p->vy) * blendRate;
                p->vz += (windVel.z - p->vz) * blendRate;
            }
            p->px += p->vx * dt;
            p->py += p->vy * dt;
            p->pz += p->vz * dt;
        }

        if (reachedTarget && activePath && activePath->arrivalForceField) {
            Vector3 position = {p->px, p->py, p->pz};
            Vector3 velocity = {p->vx, p->vy, p->vz};
            ParticleTravel_ApplyImpactEntry(activePath, &position, &velocity);
            p->px = position.x; p->py = position.y; p->pz = position.z;
            p->vx = velocity.x; p->vy = velocity.y; p->vz = velocity.z;
            p->impact_age = 0.0f;
            p->impact_active = 1.0f;
            continue;
        }
        if (reachedTarget) {
            EmissionChildrenHandle impactIndex = s_particleImpactIndex[i];
            EmissionImpact_Publish(impactIndex, s_particleImpactCount[i], &(EmissionEvent){
                .kind = EMISSION_EVENT_ARRIVAL,
                .position = {p->px, p->py, p->pz},
                .velocity = {p->vx, p->vy, p->vz}});
            ReleasePath(pathIndex);
            p->route_pad0 = -1.0f;
            EmissionImpact_Release(impactIndex);
            s_particleImpactIndex[i] = 0;
            s_particleImpactCount[i] = 0;
            p->active = 0.0f;
            continue;
        }

        if (forceIndex >= 0 && forceIndex < s_fieldCount) {
            const ForceField *ff = FieldAt(forceIndex);
            if (ff) {
                Vector3 position = {p->px, p->py, p->pz};
                Vector3 velocity = {p->vx, p->vy, p->vz};
                ForceField_ResolveParticleContacts(ff, p->radius,
                                                  &position, &velocity);
                p->px = position.x; p->py = position.y; p->pz = position.z;
                p->vx = velocity.x; p->vy = velocity.y; p->vz = velocity.z;
            }
        }
        // 4. Ground/Floor collision check (which triggers dust puffs on CPU)
        if (p->ff_pad1 >= 0.0f)
        {
            float floorY = p->ff_pad2;
            if (p->py <= floorY)
            {
                if (p->vy < 0.0f)
                    p->vy = -p->vy * p->ff_pad1;
                p->vx *= 0.8f;
                p->vz *= 0.8f;
                p->py = floorY + 0.005f;

                EmissionLegacy_CollisionDust((Vector3){p->px, floorY + 0.01f, p->pz}, LegacyDustRandom, NULL);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
static void GpuParticleSystem_DrawPass(Camera3D camera, Texture2D texture);

void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture)
{
    GpuParticleSystem_DrawLayer(camera, texture, 0);
}

void GpuParticleSystem_DrawLayer(Camera3D camera, Texture2D texture, int layer)
{
    if (s_surfacePass != 0 || !s_use_compute) {
        if (s_surfacePass != 0 || layer != 1)
            GpuParticleSystem_DrawPass(camera, texture);
        return;
    }
    unsigned int combinedMask = s_blendMask | EmissionGpu_BlendMask();
    for (int blend = VFX_BLEND_ALPHA; blend <= VFX_BLEND_ADDITIVE; blend++) {
        if (!(combinedMask & (1u << blend)) ||
            (layer == 1 && blend != VFX_BLEND_ALPHA) ||
            (layer == 2 && blend != VFX_BLEND_ADDITIVE))
            continue;
        s_drawBlend = blend;
        rlDrawRenderBatchActive();
        BeginBlendMode(blend == VFX_BLEND_ALPHA ? BLEND_ALPHA : BLEND_ADDITIVE);
        GpuParticleSystem_DrawPass(camera, texture);
        if (EmissionGpu_HasChildren()) {
            s_drawingChildren = true;
            GpuParticleSystem_DrawPass(camera, texture);
            s_drawingChildren = false;
        }
        rlDrawRenderBatchActive();
        EndBlendMode();
    }
}

static void GpuParticleSystem_DrawPass(Camera3D camera, Texture2D texture)
{
    if (!s_initialized || !s_hasSpawned)
        return;
    if (s_surfacePass != 0 && s_surfaceIndexed && s_surfaceIndexCount == 0)
        return;

    Vector3 viewDir, right, up;
    ComputeCameraBasis(camera, &viewDir, &right, &up);

    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float aspectMVP = (screenH > 0) ? ((float)screenW / (float)screenH) : 1.0f;
    double topMVP = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double rightMVP = topMVP * aspectMVP;
    Matrix matProjMVP = MatrixFrustum(-rightMVP, rightMVP, -topMVP, topMVP, 1.0, 1000.0);
    Matrix matMVP = MatrixMultiply(matView, matProjMVP);

    if (s_use_compute)
    {
        bool softParticlePass = false;
        Shader drawShader = s_draw_shader_gpu;
        const GpuDrawShaderLocs *locs = &s_draw_locs_gpu;
        if (s_surfacePass == 1 && s_surface_capture_shader_gpu.id) {
            drawShader = s_surface_capture_shader_gpu;
            locs = &s_surface_capture_locs_gpu;
        } else if (s_surfacePass == 3 && s_surface_back_shader_gpu.id) {
            drawShader = s_surface_back_shader_gpu;
            locs = &s_surface_back_locs_gpu;
        }

        BeginShaderMode(drawShader);
        if (s_surfacePass == 0) {
            float blend = (float)s_drawBlend;
            if (locs->filterBlend >= 0)
                SetShaderValue(drawShader, locs->filterBlend, &blend, SHADER_UNIFORM_FLOAT);
            if (locs->blendLaw >= 0)
                SetShaderValue(drawShader, locs->blendLaw, &s_drawBlend, SHADER_UNIFORM_INT);
        } else {
            int indexed = s_surfaceIndexed ? 1 : 0;
            if (locs->surfaceIndexed >= 0)
                SetShaderValue(drawShader, locs->surfaceIndexed, &indexed, SHADER_UNIFORM_INT);
            /* Both shader branches reflect binding 1, including legacy control. */
            rlBindShaderBuffer(s_surface_index_ssbo, 1);

            if (locs->materialId >= 0)
                SetShaderValue(drawShader, locs->materialId, &s_surfaceMaterialId, SHADER_UNIFORM_FLOAT);

            if (s_surfacePass == 3) {
                float match = s_surfaceFrontDepth.id != 0 ? 1.0f : 0.0f;
                int textureSlot = 0;
                if (locs->matchFrontMaterial >= 0)
                    SetShaderValue(drawShader, locs->matchFrontMaterial, &match, SHADER_UNIFORM_FLOAT);
                if (locs->frontDepthTex >= 0)
                    SetShaderValue(drawShader, locs->frontDepthTex, &textureSlot, SHADER_UNIFORM_INT);
            }
        }

        if (locs->right >= 0)
            SetShaderValue(drawShader, locs->right, &right, SHADER_UNIFORM_VEC3);
        if (locs->up >= 0)
            SetShaderValue(drawShader, locs->up, &up, SHADER_UNIFORM_VEC3);
        if (locs->mvp >= 0)
            SetShaderValueMatrix(drawShader, locs->mvp, matMVP);
        if (locs->view >= 0)
            SetShaderValueMatrix(drawShader, locs->view, matView);
        if (locs->projection >= 0)
            SetShaderValueMatrix(drawShader, locs->projection, matProjMVP);

        float filterEmitter = (float)s_filterEmitter, filterMode = (float)s_filterRenderMode;
        if (locs->filterEmitter >= 0)
            SetShaderValue(drawShader, locs->filterEmitter, &filterEmitter, SHADER_UNIFORM_FLOAT);
        if (locs->filterMode >= 0)
            SetShaderValue(drawShader, locs->filterMode, &filterMode, SHADER_UNIFORM_FLOAT);

        // Fluid capture/thickness shaders do not draw visible billboards and
        // must not sample the scene depth. The ordinary GPU particle pass uses
        // the same previous-frame depth contract as CPU particles.
        if (s_surfacePass == 0)
        {
            Texture2D depthTex = SceneTargets_GetDepthTexture();
            float softFade = depthTex.id != 0 ? GPU_PARTICLE_SOFT_FADE_METERS : 0.0f;
            if (softFade > 0.0f)
            {
                SceneTargets_RequestSoftDepthRegion((Rectangle){0.0f, 0.0f,
                                                       (float)screenW,
                                                       (float)screenH});
                SceneTargets_BindDepthForSoftParticles(drawShader, GPU_PARTICLE_SOFT_DEPTH_SLOT);
                softParticlePass = true;
            }
            if (locs->softFade >= 0)
                SetShaderValue(drawShader, locs->softFade, &softFade, SHADER_UNIFORM_FLOAT);
        }

        rlBindShaderBuffer(s_drawingChildren ? EmissionGpu_ChildBuffer() : s_ssbo, 0);

        if (softParticlePass)
        {
            rlDrawRenderBatchActive();
            rlDisableDepthMask();
            rlDisableDepthTest();
        }

        rlActiveTextureSlot(0);
        unsigned int drawTextureId = (s_surfacePass == 3 && s_surfaceFrontDepth.id != 0)
                                      ? s_surfaceFrontDepth.id : texture.id;
        rlEnableTexture(drawTextureId);

        rlEnableShader(drawShader.id);
        rlEnableVertexArray(s_draw_vao);
        int instanceCount = (s_surfacePass != 0 && s_surfaceIndexed)
                                ? s_surfaceIndexCount
                                : (s_drawingChildren ? EMISSION_GPU_CHILD_CAPACITY : GpuParticleSystem_ActiveCount());
        if (instanceCount > 0)
            rlDrawVertexArrayInstanced(0, 6, instanceCount);
        if (s_surfacePass != 0)
            s_surfaceLastInstanceCount = instanceCount;
        rlDisableVertexArray();

        rlDisableShader();

        if (softParticlePass)
        {
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            rlEnableDepthTest();
        }

        rlActiveTextureSlot(0);
        rlDisableTexture();
        if (s_surfacePass == 0 && softParticlePass)
            SceneTargets_UnbindSoftParticleDepth(GPU_PARTICLE_SOFT_DEPTH_SLOT);
        EndShaderMode();
    }
    else
    {
        rlSetTexture(texture.id);
        rlBegin(RL_QUADS);

        int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
        for (int i = 0; i < slotLimit; i++)
        {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f)
                continue;
            if (s_filterRenderMode < 0 && (int)p->render_mode == 3) continue;
            if (s_filterEmitter >= 0 && (int)p->emitter_id != s_filterEmitter) continue;
            if (s_filterRenderMode >= 0 && (int)p->render_mode != s_filterRenderMode) continue;

            float r = p->radius;
            if (r <= 1e-5f)
                continue;

            // Culling nhanh các hạt nằm hoàn toàn sau lưng camera
            float dx = p->px - camera.position.x;
            float dy = p->py - camera.position.y;
            float dz = p->pz - camera.position.z;
            float forwardDist = -(dx * viewDir.x + dy * viewDir.y + dz * viewDir.z);
            if (forwardDist < -r * 3.5f)
                continue;

            float t = (p->life_max > 1e-6f) ? (1.0f - (p->life_rem / p->life_max)) : 1.0f;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;

            unsigned char ca = ClampFloatToByte((p->csa + (p->cea - p->csa) * t) * 255.0f);
            if (ca == 0)
                continue;
            unsigned char cr = ClampFloatToByte((p->csr + (p->cer - p->csr) * t) * 255.0f);
            unsigned char cg = ClampFloatToByte((p->csg + (p->ceg - p->csg) * t) * 255.0f);
            unsigned char cb = ClampFloatToByte((p->csb + (p->ceb - p->csb) * t) * 255.0f);

            float cx = p->px, cy = p->py, cz = p->pz;

            rlColor4ub(cr, cg, cb, ca);

            float rx = right.x * r, ry = right.y * r, rz = right.z * r;
            float ux = up.x * r, uy = up.y * r, uz = up.z * r;

            // Velocity stretch rendering
            float stretchStrength = p->ff_pad0;
            if (stretchStrength > 0.0f)
            {
                float vx = p->vx * right.x + p->vy * right.y + p->vz * right.z;
                float vy = p->vx * up.x + p->vy * up.y + p->vz * up.z;
                float screenSpeedSq = vx * vx + vy * vy;
                if (screenSpeedSq > 0.0025f) // > 0.05f^2
                {
                    float screenSpeed = sqrtf(screenSpeedSq);
                    float invSpeed = 1.0f / screenSpeed;
                    float dirX = vx * invSpeed;
                    float dirY = vy * invSpeed;

                    Vector3 tangentDir = {
                        right.x * dirX + up.x * dirY,
                        right.y * dirX + up.y * dirY,
                        right.z * dirX + up.z * dirY
                    };
                    Vector3 rightDir = {
                        right.x * dirY - up.x * dirX,
                        right.y * dirY - up.y * dirX,
                        right.z * dirY - up.z * dirX
                    };

                    float stretchFactor = 1.0f + screenSpeed * stretchStrength;
                    if (stretchFactor > 3.5f) stretchFactor = 3.5f;

                    rx = rightDir.x * r;
                    ry = rightDir.y * r;
                    rz = rightDir.z * r;

                    float stretchedR = r * stretchFactor;
                    ux = tangentDir.x * stretchedR;
                    uy = tangentDir.y * stretchedR;
                    uz = tangentDir.z * stretchedR;
                }
            }

            rlTexCoord2f(0.0f, 0.0f);
            rlVertex3f(cx + rx - ux, cy + ry - uy, cz + rz - uz);
            rlTexCoord2f(0.0f, 1.0f);
            rlVertex3f(cx + rx + ux, cy + ry + uy, cz + rz + uz);
            rlTexCoord2f(1.0f, 1.0f);
            rlVertex3f(cx - rx + ux, cy - ry + uy, cz - rz + uz);
            rlTexCoord2f(1.0f, 0.0f);
            rlVertex3f(cx - rx - ux, cy - ry - uy, cz - rz - uz);
        }
        rlEnd();
    }
}

void GpuParticleSystem_SetSurfaceMaterialId(float materialId)
{
    s_surfaceMaterialId = materialId;
}

void GpuParticleSystem_DrawSurfaceEmitter(Camera3D camera, Texture2D texture, int emitterId)
{
    s_filterEmitter = emitterId;
    s_filterRenderMode = 3;
    s_surfacePass = 1;
    if (s_use_compute) {
        GpuParticleSystem_Draw(camera, texture);
    } else if (s_surface_capture_shader_cpu.id) {
        /* CPU path: giữ BeginShaderMode và ma trận chiếu cố định bên ngoài vòng lặp,
         * chỉ flush mỗi quad để cập nhật u_capture_params thay vì bật/tắt shader mỗi hạt. */
        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        int screenH = GetScreenHeight();
        float aspect = (screenH > 0) ? ((float)GetScreenWidth() / (float)screenH) : 1.0f;
        double top = tan(camera.fovy * 0.5 * DEG2RAD);
        double right_f = top * aspect;
        Matrix matProj = MatrixFrustum(-right_f, right_f, -top, top, 0.01, 1000.0);

        Vector3 viewTowardTarget = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 camRight = Vector3Normalize(Vector3CrossProduct(viewTowardTarget, camera.up));
        Vector3 camUp    = Vector3CrossProduct(camRight, viewTowardTarget);

        BeginShaderMode(s_surface_capture_shader_cpu);
        if (s_cpu_capture_proj_loc >= 0)
            SetShaderValueMatrix(s_surface_capture_shader_cpu, s_cpu_capture_proj_loc, matProj);
        if (s_cpu_capture_material_loc >= 0)
            SetShaderValue(s_surface_capture_shader_cpu, s_cpu_capture_material_loc,
                           &s_surfaceMaterialId, SHADER_UNIFORM_FLOAT);

        int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
        for (int i = 0; i < slotLimit; i++) {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f) continue;
            if ((int)p->render_mode != 3) continue;
            if ((int)p->emitter_id != emitterId) continue;
            if (p->life_rem <= 0.0f) continue;

            Vector3 worldPos = {p->px, p->py, p->pz};
            float r = p->radius;
            Vector3 viewPos = Vector3Transform(worldPos, matView);
            float params[4] = {viewPos.x, viewPos.y, viewPos.z, r};

            if (s_cpu_capture_params_loc >= 0)
                SetShaderValue(s_surface_capture_shader_cpu, s_cpu_capture_params_loc,
                               params, SHADER_UNIFORM_VEC4);

            Vector3 rx = {camRight.x * r, camRight.y * r, camRight.z * r};
            Vector3 uy = {camUp.x * r,    camUp.y * r,    camUp.z * r};
            float x0 = worldPos.x, y0 = worldPos.y, z0 = worldPos.z;

            rlBegin(RL_QUADS);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(x0 - rx.x + uy.x, y0 - rx.y + uy.y, z0 - rx.z + uy.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(x0 + rx.x + uy.x, y0 + rx.y + uy.y, z0 + rx.z + uy.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(x0 + rx.x - uy.x, y0 + rx.y - uy.y, z0 + rx.z - uy.z);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(x0 - rx.x - uy.x, y0 - rx.y - uy.y, z0 - rx.z - uy.z);
            rlEnd();
            rlDrawRenderBatchActive();
        }
        EndShaderMode();
    }
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
}

void GpuParticleSystem_DrawSurfaceBackEmitter(Camera3D camera, int emitterId)
{
    s_filterEmitter = emitterId;
    s_filterRenderMode = 3;
    s_surfacePass = 3;
    if (s_use_compute) {
        GpuParticleSystem_Draw(camera, (Texture2D){0});
    } else if (s_surface_back_shader_cpu.id) {
        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        int screenH = GetScreenHeight();
        float aspect = (screenH > 0) ? ((float)GetScreenWidth() / (float)screenH) : 1.0f;
        double top = tan(camera.fovy * 0.5 * DEG2RAD);
        double right_f = top * aspect;
        Matrix matProj = MatrixFrustum(-right_f, right_f, -top, top, 0.01, 1000.0);

        Vector3 viewTowardTarget = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 camRight = Vector3Normalize(Vector3CrossProduct(viewTowardTarget, camera.up));
        Vector3 camUp    = Vector3CrossProduct(camRight, viewTowardTarget);

        BeginShaderMode(s_surface_back_shader_cpu);
        if (s_cpu_back_proj_loc >= 0)
            SetShaderValueMatrix(s_surface_back_shader_cpu, s_cpu_back_proj_loc, matProj);

        int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
        for (int i = 0; i < slotLimit; i++) {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f) continue;
            if ((int)p->render_mode != 3) continue;
            if ((int)p->emitter_id != emitterId) continue;
            if (p->life_rem <= 0.0f) continue;

            Vector3 worldPos = {p->px, p->py, p->pz};
            float r = p->radius;
            Vector3 viewPos = Vector3Transform(worldPos, matView);
            float params[4] = {viewPos.x, viewPos.y, viewPos.z, r};

            if (s_cpu_back_params_loc >= 0)
                SetShaderValue(s_surface_back_shader_cpu, s_cpu_back_params_loc,
                               params, SHADER_UNIFORM_VEC4);

            Vector3 rx = {camRight.x * r, camRight.y * r, camRight.z * r};
            Vector3 uy = {camUp.x * r,    camUp.y * r,    camUp.z * r};
            float x0 = worldPos.x, y0 = worldPos.y, z0 = worldPos.z;

            rlBegin(RL_QUADS);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(x0 - rx.x + uy.x, y0 - rx.y + uy.y, z0 - rx.z + uy.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(x0 + rx.x + uy.x, y0 + rx.y + uy.y, z0 + rx.z + uy.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(x0 + rx.x - uy.x, y0 + rx.y - uy.y, z0 + rx.z - uy.z);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(x0 - rx.x - uy.x, y0 - rx.y - uy.y, z0 - rx.z - uy.z);
            rlEnd();
            rlDrawRenderBatchActive();
        }
        EndShaderMode();
    }
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
}

static bool GpuParticleSystem_PrepareSurfaceIndices(const GpuSurfaceRoute *routes, int count)
{
    if (!s_initialized || !routes || count <= 0 || count > GPU_SURFACE_MAX_STREAMS)
        return false;
    bool same = s_surfaceCacheValid && s_surfaceCachedRevision == s_surfaceRevision &&
                count == s_surfaceRouteCount;
    for (int i = 0; same && i < count; ++i)
        same = routes[i].emitterId == s_surfaceRoutes[i].emitterId &&
               routes[i].materialId == s_surfaceRoutes[i].materialId;
    if (!same) {
        const void *metadata = s_use_compute ? (const void *)s_surfaceResidentRouting
                                             : (const void *)s_cpu_pool;
        size_t stride = s_use_compute ? sizeof(GpuSurfaceRouting) : sizeof(GpuParticleData);
        size_t emitterOffset = s_use_compute ? offsetof(GpuSurfaceRouting, emitterId)
                                             : offsetof(GpuParticleData, emitter_id);
        size_t modeOffset = s_use_compute ? offsetof(GpuSurfaceRouting, renderMode)
                                          : offsetof(GpuParticleData, render_mode);
        int residentSpan = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
        int n = GpuSurfaceIndex_Build(metadata, residentSpan, stride,
                                     emitterOffset, modeOffset, routes, count,
                                     s_surfaceIndices, MAX_GPU_PARTICLES);
        if (n < 0) return false;
        memcpy(s_surfaceRoutes, routes, (size_t)count * sizeof(*routes));
        s_surfaceRouteCount = count;
        s_surfaceIndexCount = n;
        s_surfaceCachedRevision = s_surfaceRevision;
        s_surfaceCacheValid = true;
        if (s_use_compute && n > 0)
            rlUpdateShaderBuffer(s_surface_index_ssbo, s_surfaceIndices,
                                 (unsigned int)((size_t)n * sizeof(GpuSurfaceIndex)), 0);
    }
    s_surfaceLastInstanceCount = s_surfaceIndexCount;
    return true;
}

bool GpuParticleSystem_DrawSurfaceEmitters(Camera3D camera, Texture2D texture,
                                          const GpuSurfaceRoute *routes, int count)
{
    if (!GpuParticleSystem_PrepareSurfaceIndices(routes, count)) return false;
    if (!s_use_compute) {
        float savedMaterial = s_surfaceMaterialId;
        for (int i = 0; i < count; ++i) {
            s_surfaceMaterialId = routes[i].materialId;
            GpuParticleSystem_DrawSurfaceEmitter(camera, texture, routes[i].emitterId);
        }
        s_surfaceMaterialId = savedMaterial;
        return true;
    }
    s_surfaceIndexed = true;
    s_surfacePass = 1;
    s_filterEmitter = -1;
    s_filterRenderMode = 3;
    GpuParticleSystem_Draw(camera, texture);
    s_surfaceIndexed = false;
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
    return true;
}

bool GpuParticleSystem_DrawSurfaceBackEmitters(Camera3D camera,
                                              const GpuSurfaceRoute *routes, int count)
{
    if (!GpuParticleSystem_PrepareSurfaceIndices(routes, count)) return false;
    if (!s_use_compute) {
        for (int i = 0; i < count; ++i)
            GpuParticleSystem_DrawSurfaceBackEmitter(camera, routes[i].emitterId);
        return true;
    }
    s_surfaceIndexed = true;
    s_surfacePass = 3;
    s_filterEmitter = -1;
    s_filterRenderMode = 3;
    GpuParticleSystem_Draw(camera, (Texture2D){0});
    s_surfaceIndexed = false;
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
    return true;
}

int GpuParticleSystem_GetSurfaceCaptureInstanceCount(void)
{
    return s_surfaceLastInstanceCount;
}

bool GpuParticleSystem_IsForceFieldInUse(const ForceField *field)
{
    return s_initialized && GpuParticleFieldLease_InUse(s_fieldLeases,
                                                       MAX_GPU_FORCE_FIELDS, field);
}

void GpuParticleSystem_SetSurfaceCaptureFrontDepth(Texture2D texture)
{
    s_surfaceFrontDepth = texture;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void GpuParticleSystem_Unload(void)
{
    EmissionGpu_Unload();
    if (!s_initialized)
        return;

    int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
    for (int i = 0; i < slotLimit; ++i) {
        if (s_particleImpactIndex[i] != 0) {
            EmissionImpact_Release(s_particleImpactIndex[i]);
            s_particleImpactIndex[i] = 0;
        }
        s_particleImpactCount[i] = 0;
    }
    memset(s_fieldLeases, 0, sizeof(s_fieldLeases));
    s_fieldCount = 0;
    memset(s_pathRegistry, 0, sizeof(s_pathRegistry));
    memset(s_pathRefs, 0, sizeof(s_pathRefs));
    s_activePathSlots = 0;

    if (s_use_compute)
    {
        if (s_surface_index_ssbo) {
            rlUnloadShaderBuffer(s_surface_index_ssbo);
            s_surface_index_ssbo = 0;
        }
        if (s_ssbo)
        {
            rlUnloadShaderBuffer(s_ssbo);
            s_ssbo = 0;
        }
        if (s_ff_ssbo)
        {
            rlUnloadShaderBuffer(s_ff_ssbo);
            s_ff_ssbo = 0;
        }
        if (s_path_ssbo)
        {
            rlUnloadShaderBuffer(s_path_ssbo);
            s_path_ssbo = 0;
        }
        if (s_wind_ssbo)
        {
            rlUnloadShaderBuffer(s_wind_ssbo);
            s_wind_ssbo = 0;
        }
        if (s_wind_terrain_ssbo)
        {
            rlUnloadShaderBuffer(s_wind_terrain_ssbo);
            s_wind_terrain_ssbo = 0;
        }
        if (s_motion_scene_ssbo) {
            rlUnloadShaderBuffer(s_motion_scene_ssbo);
            s_motion_scene_ssbo = 0;
        }
        if (s_motion_body_ssbo) {
            rlUnloadShaderBuffer(s_motion_body_ssbo);
            s_motion_body_ssbo = 0;
        }
        if (s_draw_vao)
        {
            rlUnloadVertexArray(s_draw_vao);
            s_draw_vao = 0;
        }
        if (s_draw_quad_vbo)
        {
            rlUnloadVertexBuffer(s_draw_quad_vbo);
            s_draw_quad_vbo = 0;
        }
        if (s_compute_prog)
        {
            rlDisableShader();
            rlUnloadShaderProgram(s_compute_prog);
            s_compute_prog = 0;
        }
    }
    s_initialized = false;
    s_use_compute = false;
    s_hasSpawned = false;
    s_spawn_cursor = 0;
    s_motionLiveCount = 0;
    s_blendMask = 0;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------
bool GpuParticleSystem_IsComputeActive(void) { return s_use_compute; }

int GpuParticleSystem_ActiveCount(void)
{
    if (!s_initialized)
        return 0;
    if (s_use_compute)
        return s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
    int count = 0;
    int slotLimit = s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
    for (int i = 0; i < slotLimit; i++)
    {
        if (s_cpu_pool[i].active >= 0.5f)
            count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Debug overlay
// ---------------------------------------------------------------------------
void GpuParticleSystem_DrawDebug(int x, int y)
{
    if (!s_initialized)
    {
        DrawText("GpuParticles: NOT INIT", x, y, 18, RED);
        return;
    }

    const char *path_name = s_use_compute ? "COMPUTE (GPU)" : "CPU / VBO";
    Color path_color = s_use_compute ? GREEN : YELLOW;

    int rl_ver = rlGetVersion();
    const char *ver_label =
        rl_ver == 4 ? "GL 4.3+" : rl_ver == 3 ? "GL 3.3"
                              : rl_ver == 5   ? "GLES 2.0"
                              : rl_ver == 6   ? "GLES 3.x"
                                              : "Vulkan / GL";

    int active = GpuParticleSystem_ActiveCount();

    DrawText(TextFormat("GpuParticles  [%s]", path_name), x, y, 18, path_color);
    DrawText(TextFormat("API: %s", ver_label), x, y + 22, 14, LIGHTGRAY);
    DrawText(TextFormat("Pool: %d / %d active", active, MAX_GPU_PARTICLES), x, y + 40, 14, LIGHTGRAY);
}
