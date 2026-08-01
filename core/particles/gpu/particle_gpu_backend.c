#include "particle_gpu_legacy.h"
#include "core/resource_manager.h"
#include "core/particles/particle_system.h"
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
} GpuParticleData;

// ---------------------------------------------------------------------------
// Force field registry — map con trỏ ForceField (CPU) -> slot GPU.
// Chỉ có hiệu lực ở COMPUTE path; CPU/VBO fallback bỏ qua force field.
// Không sửa core/force_field.h — dùng nguyên ForceFieldGPU/ForceField_PackGPU
// đã khai báo sẵn ở đó.
// ---------------------------------------------------------------------------
#define MAX_GPU_FORCE_FIELDS 8

static const ForceField *s_fieldRegistry[MAX_GPU_FORCE_FIELDS];
static Vector3 s_fieldAxisOrigin[MAX_GPU_FORCE_FIELDS];
static Vector3 s_fieldAxisDir[MAX_GPU_FORCE_FIELDS];
static int s_fieldCount = 0;

static int RegisterField(const ForceField *ff, Vector3 axisOrigin, Vector3 axisDir)
{
    if (!ff)
        return -1;
    for (int i = 0; i < s_fieldCount; i++)
    {
        if (s_fieldRegistry[i] == ff)
        {
            // Refresh trục — particle mới nhất spawn cùng field quyết định
            // trục dùng cho slot này ở lần pack tiếp theo.
            s_fieldAxisOrigin[i] = axisOrigin;
            s_fieldAxisDir[i] = axisDir;
            return i;
        }
    }
    if (s_fieldCount >= MAX_GPU_FORCE_FIELDS)
    {
        TraceLog(LOG_WARNING, "GPU_PARTICLES: force field registry full (%d), ignoring", MAX_GPU_FORCE_FIELDS);
        return -1;
    }
    int idx = s_fieldCount++;
    s_fieldRegistry[idx] = ff;
    s_fieldAxisOrigin[idx] = axisOrigin;
    s_fieldAxisDir[idx] = axisDir;
    return idx;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool s_initialized = false;
static bool s_use_compute = false;

static unsigned int s_ssbo = 0;
static unsigned int s_ff_ssbo = 0; // ForceFieldBuffer, binding = 1
static unsigned int s_compute_prog = 0;
static unsigned int s_draw_vao = 0;
static unsigned int s_draw_quad_vbo = 0; // template quad, attribute 0
static Shader s_draw_shader_gpu = {0};
static Shader s_surface_capture_shader_gpu = {0};
static Shader s_surface_thickness_shader_gpu = {0};
static Shader s_surface_capture_shader_cpu = {0};
static float s_elapsed_time = 0.0f;

static GpuParticleData s_cpu_pool[MAX_GPU_PARTICLES];

static int s_spawn_cursor = 0;
static int s_spawn_start_this_frame = -1;
static int s_spawn_count_this_frame = 0;
static int s_filterEmitter = -1, s_filterRenderMode = -1;
static int s_surfacePass = 0; /* 0 normal, 1 depth, 2 thickness */

// Vector field textures cho FORCE_VECTOR_TEXTURE — không sở hữu (không Unload
// ở đây), chỉ bind vào texture unit trước mỗi dispatch khi slot đang set.
static Texture2D s_vectorFieldTex[GPU_VECTOR_FIELD_SLOTS] = {0};

// Matches the CPU particle default. GPU billboards sample the prior frame's
// linear scene-depth snapshot, so their intersection with terrain fades rather
// than being clipped by a hard depth-test rail.
#define GPU_PARTICLE_SOFT_DEPTH_SLOT 3
#define GPU_PARTICLE_SOFT_FADE_METERS 0.35f

// ---------------------------------------------------------------------------
// Compute shader loader
// Source dùng #version 310 es (GLES 3.1).
// Desktop GL 4.3: runtime patch lên #version 430 core.
// ---------------------------------------------------------------------------
static unsigned int CompileComputeShader(const char *path)
{
    char *src = LoadFileText(path);
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
    UnloadFileText(src);

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
    memset(s_vectorFieldTex, 0, sizeof(s_vectorFieldTex));
    s_spawn_cursor = 0;
    s_spawn_start_this_frame = -1;
    s_spawn_count_this_frame = 0;
    s_fieldCount = 0;
    s_elapsed_time = 0.0f;

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

        s_ssbo = rlLoadShaderBuffer(MAX_GPU_PARTICLES * (ptrdiff_t)sizeof(GpuParticleData), NULL, RL_DYNAMIC_DRAW);

        s_ff_ssbo = rlLoadShaderBuffer(MAX_GPU_FORCE_FIELDS * (ptrdiff_t)sizeof(ForceFieldGPU), NULL, RL_DYNAMIC_DRAW);

        s_draw_shader_gpu = ResourceManager_LoadShader(ssbo_vs_path, fs_path);
        if (s_draw_shader_gpu.id == 0)
        {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: draw shader compile failed, fallback CPU");
            rlDisableShader();
            rlUnloadShaderProgram(s_compute_prog);
            s_compute_prog = 0;
            rlUnloadShaderBuffer(s_ssbo);
            s_ssbo = 0;
            rlUnloadShaderBuffer(s_ff_ssbo);
            s_ff_ssbo = 0;
            goto cpu_path;
        }
        s_surface_capture_shader_gpu = ResourceManager_LoadShader("core/particles/shaders/gpu/fluid_surface_capture.vs", "core/fluid/shaders/fluid_capture_particle.fs");
        s_surface_thickness_shader_gpu = ResourceManager_LoadShader("core/particles/shaders/gpu/fluid_surface_capture.vs", "core/fluid/shaders/fluid_surface_thickness.fs");
        if (s_surface_capture_shader_gpu.id == 0)
        {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: surface capture shader unavailable");
        }

        // Template quad (2 tam giác, góc ±1, CCW = front-face) ở attribute 0.
        // Per-particle data đi qua SSBO binding 0 đọc bằng gl_InstanceID trong VS.
        static const float quadTemplate[18] = {
            -1.0f,
            -1.0f,
            0.0f,
            1.0f,
            -1.0f,
            0.0f,
            1.0f,
            1.0f,
            0.0f,
            -1.0f,
            -1.0f,
            0.0f,
            1.0f,
            1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
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
    s_surface_capture_shader_cpu = ResourceManager_LoadShader(NULL, "core/fluid/shaders/fluid_capture.fs");
        // ----- CPU/VBO PATH -----
        s_use_compute = false;
        TraceLog(LOG_INFO, "GPU_PARTICLES: CPU/VBO path active (%d particles)", MAX_GPU_PARTICLES);
    }

    s_initialized = true;
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------
void GpuParticleSystem_Spawn(GpuParticleConfig cfg)
{
    if (!s_initialized)
        return;

    int idx = s_spawn_cursor % MAX_GPU_PARTICLES;
    s_spawn_cursor++;

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
    d.csr = (cfg.colorStart.r / 255.0f) * boost;
    d.csg = (cfg.colorStart.g / 255.0f) * boost;
    d.csb = (cfg.colorStart.b / 255.0f) * boost;
    d.csa = cfg.colorStart.a / 255.0f;
    d.cer = (cfg.colorEnd.r / 255.0f) * boost;
    d.ceg = (cfg.colorEnd.g / 255.0f) * boost;
    d.ceb = (cfg.colorEnd.b / 255.0f) * boost;
    d.cea = cfg.colorEnd.a / 255.0f;
    d.life_rem = cfg.lifetime;
    d.life_max = cfg.lifetime;
    d.phase = (float)GetRandomValue(0, 10000) / 10000.0f;
    d.active = 1.0f;
    d.ff_index = (float)RegisterField(cfg.forceField, cfg.axisOrigin, cfg.axisDir);
    d.ff_pad0 = cfg.stretchStrength;
    d.ff_pad1 = cfg.collisionEnabled ? cfg.collisionElasticity : -1.0f;
    d.ff_pad2 = cfg.collisionFloorY;
    d.emitter_id = (float)cfg.emitterId;
    d.render_mode = (float)cfg.renderMode;
    d.route_pad0 = d.route_pad1 = 0.0f;

    s_cpu_pool[idx] = d;

    if (s_use_compute)
    {
        if (s_spawn_start_this_frame == -1)
            s_spawn_start_this_frame = idx;
        s_spawn_count_this_frame++;
    }
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
void GpuParticleSystem_Update(float dt)
{
    if (!s_initialized)
        return;

    if (s_use_compute)
    {
        s_elapsed_time += dt;

        // Batch upload cho các hạt mới spawn trong frame này
        if (s_spawn_count_this_frame > 0)
        {
            int start = s_spawn_start_this_frame;
            int count = s_spawn_count_this_frame > MAX_GPU_PARTICLES ? MAX_GPU_PARTICLES : s_spawn_count_this_frame;
            int end = start + count;

            if (end <= MAX_GPU_PARTICLES)
            {
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[start], count * sizeof(GpuParticleData), start * sizeof(GpuParticleData));
            }
            else
            {
                int chunk1 = MAX_GPU_PARTICLES - start;
                int chunk2 = count - chunk1;
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[start], chunk1 * sizeof(GpuParticleData), start * sizeof(GpuParticleData));
                rlUpdateShaderBuffer(s_ssbo, &s_cpu_pool[0], chunk2 * sizeof(GpuParticleData), 0);
            }
            s_spawn_start_this_frame = -1;
            s_spawn_count_this_frame = 0;
        }

        // Re-pack mọi force field đã đăng ký
        if (s_fieldCount > 0)
        {
            ForceFieldGPU packed[MAX_GPU_FORCE_FIELDS];
            for (int i = 0; i < s_fieldCount; i++)
            {
                ForceField_PackGPU(s_fieldRegistry[i], s_fieldAxisOrigin[i],
                                   s_fieldAxisDir[i], &packed[i]);
            }
            rlUpdateShaderBuffer(s_ff_ssbo, packed, s_fieldCount * (ptrdiff_t)sizeof(ForceFieldGPU), 0);
        }

        rlEnableShader(s_compute_prog);
        int loc_dt = rlGetLocationUniform(s_compute_prog, "u_dt");
        if (loc_dt >= 0)
            rlSetUniform(loc_dt, &dt, RL_SHADER_UNIFORM_FLOAT, 1);
        int loc_time = rlGetLocationUniform(s_compute_prog, "u_time");
        if (loc_time >= 0)
            rlSetUniform(loc_time, &s_elapsed_time, RL_SHADER_UNIFORM_FLOAT, 1);

        for (int slot = 0; slot < GPU_VECTOR_FIELD_SLOTS; slot++)
        {
            if (s_vectorFieldTex[slot].id == 0)
                continue;
            rlActiveTextureSlot(slot);
            rlEnableTexture(s_vectorFieldTex[slot].id);
            char uname[24];
            snprintf(uname, sizeof(uname), "uVectorField%d", slot);
            int loc_tex = rlGetLocationUniform(s_compute_prog, uname);
            if (loc_tex >= 0)
                rlSetUniform(loc_tex, &slot, RL_SHADER_UNIFORM_INT, 1);
        }

        rlBindShaderBuffer(s_ssbo, 0);
        rlBindShaderBuffer(s_ff_ssbo, 1);
        unsigned int groups = (MAX_GPU_PARTICLES + 255) / 256;
        rlComputeShaderDispatch(groups, 1, 1);
        rlDisableShader();

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
    // If s_use_compute is false, this loop also integrates positions.
    // If s_use_compute is true, it replicates physics in sync with GPU compute so the CPU can detect collision.
    for (int i = 0; i < MAX_GPU_PARTICLES; i++)
    {
        GpuParticleData *p = &s_cpu_pool[i];
        if (p->active < 0.5f)
            continue;
        p->life_rem -= dt;
        if (p->life_rem <= 0.0f)
        {
            p->active = 0.0f;
            continue;
        }

        // 1. Evaluate Force Field on CPU
        if (p->ff_index >= 0.0f)
        {
            int ff_idx = (int)p->ff_index;
            if (ff_idx < s_fieldCount)
            {
                const ForceField *ff = s_fieldRegistry[ff_idx];
                Vector3 pos = {p->px, p->py, p->pz};
                Vector3 vel = {p->vx, p->vy, p->vz};
                Vector3 acc = ForceField_Evaluate(ff, pos, vel, s_elapsed_time, s_fieldAxisOrigin[ff_idx], s_fieldAxisDir[ff_idx]);
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
        float drag_f = 1.0f - p->drag * dt;
        if (drag_f < 0.0f)
            drag_f = 0.0f;
        p->vx *= drag_f;
        p->vy *= drag_f;
        p->vz *= drag_f;

        // 3. Integrate position
        p->px += p->vx * dt;
        p->py += p->vy * dt;
        p->pz += p->vz * dt;

        // 4. Ground/Floor collision check (which triggers dust puffs on CPU)
        if (p->ff_pad1 >= 0.0f)
        {
            float floorY = p->ff_pad2;
            if (p->py <= floorY)
            {
                p->vy = -p->vy * p->ff_pad1;
                p->vx *= 0.8f;
                p->vz *= 0.8f;
                p->py = floorY + 0.005f;

                // Spawn small CPU dust puffs
                static bool s_gpuDustInit = false;
                static ParticleConfig s_gpuDustConfig;
                static SkillCurve s_gpuDustCurve;
                if (!s_gpuDustInit)
                {
                    s_gpuDustConfig = (ParticleConfig){0};
                    s_gpuDustConfig.lifetime = 0.4f;
                    s_gpuDustConfig.radius = 0.12f;
                    s_gpuDustConfig.colorStart = (Color){200, 200, 200, 140};
                    s_gpuDustConfig.colorEnd = (Color){220, 220, 220, 0};
                    FloatCurve_AddStop(&s_gpuDustCurve, 0.0f, 0.2f);
                    FloatCurve_AddStop(&s_gpuDustCurve, 0.5f, 1.0f);
                    FloatCurve_AddStop(&s_gpuDustCurve, 1.0f, 1.2f);
                    s_gpuDustConfig.radiusCurve = &s_gpuDustCurve;
                    s_gpuDustInit = true;
                }

                for (int c = 0; c < 3; c++)
                {
                    ParticleConfig tempColl = s_gpuDustConfig;
                    tempColl.position = (Vector3){p->px, floorY + 0.01f, p->pz};
                    float ang = ((float)GetRandomValue(0, 359)) * DEG2RAD;
                    float spd = (float)GetRandomValue(100, 200) * 0.01f;
                    tempColl.velocity = (Vector3){
                        cosf(ang) * spd,
                        (float)GetRandomValue(80, 180) * 0.01f,
                        sinf(ang) * spd
                    };
                    SpawnParticle(tempColl);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture)
{
    if (!s_initialized)
        return;

    Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(camera.up, viewDir));
    Vector3 up = Vector3CrossProduct(viewDir, right);

    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    float aspectMVP = (float)GetScreenWidth() / (float)GetScreenHeight();
    double topMVP = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double rightMVP = topMVP * aspectMVP;
    Matrix matProjMVP = MatrixFrustum(-rightMVP, rightMVP, -topMVP, topMVP, 1.0, 1000.0);
    Matrix matMVP = MatrixMultiply(matView, matProjMVP);

    if (s_use_compute)
    {
        bool softParticlePass = false;
        Shader drawShader = s_draw_shader_gpu;
        if (s_surfacePass == 1 && s_surface_capture_shader_gpu.id) drawShader = s_surface_capture_shader_gpu;
        if (s_surfacePass == 2 && s_surface_thickness_shader_gpu.id) drawShader = s_surface_thickness_shader_gpu;
        BeginShaderMode(drawShader);

        int loc_right = GetShaderLocation(drawShader, "u_right");
        int loc_up = GetShaderLocation(drawShader, "u_up");
        int loc_mvp = GetShaderLocation(drawShader, "mvp");
        int loc_filterEmitter = GetShaderLocation(drawShader, "u_filterEmitter");
        int loc_filterMode = GetShaderLocation(drawShader, "u_filterRenderMode");
        if (loc_right >= 0)
            SetShaderValue(drawShader, loc_right, &right, SHADER_UNIFORM_VEC3);
        if (loc_up >= 0)
            SetShaderValue(drawShader, loc_up, &up, SHADER_UNIFORM_VEC3);
        if (loc_mvp >= 0)
            SetShaderValueMatrix(drawShader, loc_mvp, matMVP);
        int loc_view = GetShaderLocation(drawShader, "u_view");
        int loc_projection = GetShaderLocation(drawShader, "u_projection");
        if (loc_view >= 0) SetShaderValueMatrix(drawShader, loc_view, matView);
        if (loc_projection >= 0) SetShaderValueMatrix(drawShader, loc_projection, matProjMVP);
        float filterEmitter = (float)s_filterEmitter, filterMode = (float)s_filterRenderMode;
        if (loc_filterEmitter >= 0) SetShaderValue(drawShader, loc_filterEmitter, &filterEmitter, SHADER_UNIFORM_FLOAT);
        if (loc_filterMode >= 0) SetShaderValue(drawShader, loc_filterMode, &filterMode, SHADER_UNIFORM_FLOAT);

        // Fluid capture/thickness shaders do not draw visible billboards and
        // must not sample the scene depth. The ordinary GPU particle pass uses
        // the same previous-frame depth contract as CPU particles.
        if (s_surfacePass == 0)
        {
            Texture2D depthTex = ScreenDistort_GetDepthTexture();
            float softFade = depthTex.id != 0 ? GPU_PARTICLE_SOFT_FADE_METERS : 0.0f;
            int loc_softFade = GetShaderLocation(drawShader, "u_softFade");
            if (softFade > 0.0f)
            {
                ScreenDistort_BindDepthForSoftParticles(drawShader, GPU_PARTICLE_SOFT_DEPTH_SLOT);
                softParticlePass = true;
            }
            if (loc_softFade >= 0)
                SetShaderValue(drawShader, loc_softFade, &softFade, SHADER_UNIFORM_FLOAT);
        }

        rlBindShaderBuffer(s_ssbo, 0);

        // A soft-particle fragment behind the ground must reach the shader so
        // its alpha can fade. Flush around both depth state changes: queued
        // raylib geometry otherwise receives the wrong raster state.
        if (softParticlePass)
        {
            rlDrawRenderBatchActive();
            rlDisableDepthMask();
            rlDisableDepthTest();
        }

        // Đảm bảo kết nối texture đúng khe và báo hiệu rõ cho Vulkan/OpenGL
        rlActiveTextureSlot(0);
        rlEnableTexture(texture.id);

        rlEnableShader(drawShader.id);
        rlEnableVertexArray(s_draw_vao);
        rlDrawVertexArrayInstanced(0, 6, MAX_GPU_PARTICLES);
        rlDisableVertexArray();

        rlDisableShader();

        if (softParticlePass)
        {
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            rlEnableDepthTest();
        }

        // Gỡ texture bằng API tường minh thay vì rlSetTexture(0)
        rlActiveTextureSlot(0);
        rlDisableTexture();
        if (s_surfacePass == 0)
            ScreenDistort_UnbindSoftParticleDepth(GPU_PARTICLE_SOFT_DEPTH_SLOT);
        EndShaderMode();
    }
    else
    {
        rlSetTexture(texture.id);
        rlBegin(RL_QUADS);

        for (int i = 0; i < MAX_GPU_PARTICLES; i++)
        {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f)
                continue;
            if (s_filterRenderMode < 0 && (int)p->render_mode == 3) continue;
            if (s_filterEmitter >= 0 && (int)p->emitter_id != s_filterEmitter) continue;
            if (s_filterRenderMode >= 0 && (int)p->render_mode != s_filterRenderMode) continue;

            float t = 1.0f - (p->life_rem / p->life_max);

            unsigned char cr = (unsigned char)((p->csr + (p->cer - p->csr) * t) * 255.0f);
            unsigned char cg = (unsigned char)((p->csg + (p->ceg - p->csg) * t) * 255.0f);
            unsigned char cb = (unsigned char)((p->csb + (p->ceb - p->csb) * t) * 255.0f);
            unsigned char ca = (unsigned char)((p->csa + (p->cea - p->csa) * t) * 255.0f);

            float cx = p->px, cy = p->py, cz = p->pz, r = p->radius;

            rlColor4ub(cr, cg, cb, ca);

            float rx = right.x * r, ry = right.y * r, rz = right.z * r;
            float ux = up.x * r, uy = up.y * r, uz = up.z * r;

            // Velocity stretch rendering
            float stretchStrength = p->ff_pad0;
            if (stretchStrength > 0.0f)
            {
                Vector3 vel = {p->vx, p->vy, p->vz};
                float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
                if (speed > 0.2f)
                {
                    Vector3 velDir = {vel.x / speed, vel.y / speed, vel.z / speed};
                    Vector3 tangent = velDir;
                    
                    // Find perpendicular right vector using cross product
                    Vector3 crossV = {
                        camera.up.y * tangent.z - camera.up.z * tangent.y,
                        camera.up.z * tangent.x - camera.up.x * tangent.z,
                        camera.up.x * tangent.y - camera.up.y * tangent.x
                    };
                    float crossLen = sqrtf(crossV.x * crossV.x + crossV.y * crossV.y + crossV.z * crossV.z);
                    Vector3 rVec = right;
                    if (crossLen > 0.0f)
                    {
                        rVec.x = crossV.x / crossLen;
                        rVec.y = crossV.y / crossLen;
                        rVec.z = crossV.z / crossLen;
                    }
                    
                    float stretchFactor = 1.0f + speed * stretchStrength;
                    rx = rVec.x * r;
                    ry = rVec.y * r;
                    rz = rVec.z * r;

                    ux = tangent.x * r * stretchFactor;
                    uy = tangent.y * r * stretchFactor;
                    uz = tangent.z * r * stretchFactor;
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

void GpuParticleSystem_DrawSurfaceEmitter(Camera3D camera, Texture2D texture, int emitterId)
{
    s_filterEmitter = emitterId;
    s_filterRenderMode = 3;
    s_surfacePass = 1;
    if (!s_use_compute && s_surface_capture_shader_cpu.id) BeginShaderMode(s_surface_capture_shader_cpu);
    GpuParticleSystem_Draw(camera, texture);
    if (!s_use_compute && s_surface_capture_shader_cpu.id) EndShaderMode();
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
}

void GpuParticleSystem_DrawSurfaceThicknessEmitter(Camera3D camera, int emitterId)
{
    s_filterEmitter = emitterId;
    s_filterRenderMode = 3;
    s_surfacePass = 2;
    /* texture0 is unused by the analytic thickness shader. */
    GpuParticleSystem_Draw(camera, (Texture2D){0});
    s_surfacePass = 0;
    s_filterEmitter = s_filterRenderMode = -1;
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void GpuParticleSystem_Unload(void)
{
    if (!s_initialized)
        return;
    if (s_use_compute)
    {
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
    for (int i = 0; i < MAX_GPU_PARTICLES; i++)
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
                                              : "GL ?";

    int active = GpuParticleSystem_ActiveCount();

    DrawText(TextFormat("GpuParticles  [%s]", path_name), x, y, 18, path_color);
    DrawText(TextFormat("API: %s", ver_label), x, y + 22, 14, LIGHTGRAY);
    DrawText(TextFormat("Pool: %d / %d active", active, MAX_GPU_PARTICLES), x, y + 40, 14, LIGHTGRAY);
}
