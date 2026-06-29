#include "gpu_particle_system.h"
#include "resource_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Platform proc-address loader
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #include <windows.h>
    static void *s_LoadProc(const char *name) { return (void *)wglGetProcAddress(name); }
#elif defined(__ANDROID__)
    #include <GLES3/gl31.h>
    #include <EGL/egl.h>
    static void *s_LoadProc(const char *name) { return (void *)eglGetProcAddress(name); }
#elif defined(__linux__)
    #include <GL/glx.h>
    static void *s_LoadProc(const char *name) {
        return (void *)glXGetProcAddressARB((const GLubyte *)name);
    }
#else
    // macOS: GL 4.1 max, compute không hỗ trợ
    static void *s_LoadProc(const char *name) { (void)name; return NULL; }
#endif

// ---------------------------------------------------------------------------
// Proc types — compute + buffer operations
// ---------------------------------------------------------------------------
typedef void     (*pfn_Dispatch)    (unsigned int nx, unsigned int ny, unsigned int nz);
typedef void     (*pfn_MemBarrier)  (unsigned int barriers);
typedef void     (*pfn_BindBufBase) (unsigned int target, unsigned int index, unsigned int buffer);
typedef unsigned int (*pfn_CreateShader)(unsigned int type);
typedef void     (*pfn_ShaderSrc)   (unsigned int shader, int count, const char **str, const int *len);
typedef void     (*pfn_CompileShader)(unsigned int shader);
typedef void     (*pfn_GetShaderiv) (unsigned int shader, unsigned int pname, int *params);
typedef void     (*pfn_GetShaderLog)(unsigned int shader, int maxLen, int *len, char *log);
typedef unsigned int (*pfn_CreateProg)(void);
typedef void     (*pfn_AttachShader)(unsigned int prog, unsigned int shader);
typedef void     (*pfn_LinkProg)    (unsigned int prog);
typedef void     (*pfn_GetProgiv)   (unsigned int prog, unsigned int pname, int *params);
typedef void     (*pfn_GetProgLog)  (unsigned int prog, int maxLen, int *len, char *log);
typedef void     (*pfn_UseProgram)  (unsigned int prog);
typedef void     (*pfn_DeleteShader)(unsigned int shader);
typedef int      (*pfn_GetUniformLoc)(unsigned int prog, const char *name);
typedef void     (*pfn_Uniform1f)   (int loc, float v);
typedef void     (*pfn_Uniform1i)   (int loc, int v);
// GL 1.5 buffer ops — proc-loaded để tránh phụ thuộc header platform
typedef void     (*pfn_GenBuffers)  (int n, unsigned int *buffers);
typedef void     (*pfn_BindBuffer)  (unsigned int target, unsigned int buffer);
typedef void     (*pfn_BufData)     (unsigned int target, ptrdiff_t size, const void *data, unsigned int usage);
typedef void     (*pfn_BufSubData)  (unsigned int target, ptrdiff_t offset, ptrdiff_t size, const void *data);
// Optional for logging
typedef const unsigned char *(*pfn_GetString)(unsigned int name);

#define GL_SHADER_STORAGE_BUFFER           0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT      0x00002000
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_COMPUTE_SHADER                  0x91B9
#define GL_COMPILE_STATUS                  0x8B81
#define GL_LINK_STATUS                     0x8B82

static pfn_Dispatch      s_glDispatchCompute     = NULL;
static pfn_MemBarrier    s_glMemoryBarrier        = NULL;
static pfn_BindBufBase   s_glBindBufferBase       = NULL;
static pfn_CreateShader  s_glCreateShader         = NULL;
static pfn_ShaderSrc     s_glShaderSource         = NULL;
static pfn_CompileShader s_glCompileShader        = NULL;
static pfn_GetShaderiv   s_glGetShaderiv          = NULL;
static pfn_GetShaderLog  s_glGetShaderInfoLog     = NULL;
static pfn_CreateProg    s_glCreateProgram        = NULL;
static pfn_AttachShader  s_glAttachShader         = NULL;
static pfn_LinkProg      s_glLinkProgram          = NULL;
static pfn_GetProgiv     s_glGetProgramiv         = NULL;
static pfn_GetProgLog    s_glGetProgramInfoLog    = NULL;
static pfn_UseProgram    s_glUseProgram           = NULL;
static pfn_DeleteShader  s_glDeleteShader         = NULL;
static pfn_GetUniformLoc s_glGetUniformLocation   = NULL;
static pfn_Uniform1f     s_glUniform1f            = NULL;
static pfn_Uniform1i     s_glUniform1i            = NULL;
static pfn_GenBuffers    s_glGenBuffers           = NULL;
static pfn_BindBuffer    s_glBindBuffer           = NULL;
static pfn_BufData       s_glBufferData           = NULL;
static pfn_BufSubData    s_glBufferSubData        = NULL;

#define LOAD_PROC(type, name) s_##name = (type)s_LoadProc(#name)

// Returns true nếu compute procs load thành công.
// macOS: s_LoadProc luôn NULL → false → CPU path.
static bool LoadComputeProcs(void) {
    LOAD_PROC(pfn_Dispatch,      glDispatchCompute);
    LOAD_PROC(pfn_MemBarrier,    glMemoryBarrier);
    LOAD_PROC(pfn_BindBufBase,   glBindBufferBase);
    LOAD_PROC(pfn_CreateShader,  glCreateShader);
    LOAD_PROC(pfn_ShaderSrc,     glShaderSource);
    LOAD_PROC(pfn_CompileShader, glCompileShader);
    LOAD_PROC(pfn_GetShaderiv,   glGetShaderiv);
    LOAD_PROC(pfn_GetShaderLog,  glGetShaderInfoLog);
    LOAD_PROC(pfn_CreateProg,    glCreateProgram);
    LOAD_PROC(pfn_AttachShader,  glAttachShader);
    LOAD_PROC(pfn_LinkProg,      glLinkProgram);
    LOAD_PROC(pfn_GetProgiv,     glGetProgramiv);
    LOAD_PROC(pfn_GetProgLog,    glGetProgramInfoLog);
    LOAD_PROC(pfn_UseProgram,    glUseProgram);
    LOAD_PROC(pfn_DeleteShader,  glDeleteShader);
    LOAD_PROC(pfn_GetUniformLoc, glGetUniformLocation);
    LOAD_PROC(pfn_Uniform1f,     glUniform1f);
    LOAD_PROC(pfn_Uniform1i,     glUniform1i);
    // Buffer ops (GL 1.5 core) — cần cho SSBO setup/update
    LOAD_PROC(pfn_GenBuffers,    glGenBuffers);
    LOAD_PROC(pfn_BindBuffer,    glBindBuffer);
    LOAD_PROC(pfn_BufData,       glBufferData);
    LOAD_PROC(pfn_BufSubData,    glBufferSubData);
    return (s_glDispatchCompute != NULL && s_glMemoryBarrier != NULL);
}

// ---------------------------------------------------------------------------
// GPU particle data — phải khớp struct trong .comp và _ssbo.vs
// ---------------------------------------------------------------------------
typedef struct {
    float px, py, pz, radius;
    float vx, vy, vz, drag;
    float csr, csg, csb, csa;
    float cer, ceg, ceb, cea;
    float life_rem, life_max, phase, active;
} GpuParticleData;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool            s_initialized     = false;
static bool            s_use_compute     = false;

static unsigned int    s_ssbo            = 0;
static unsigned int    s_compute_prog    = 0;
static unsigned int    s_draw_vao        = 0;
static Shader          s_draw_shader_gpu = {0};

static GpuParticleData s_cpu_pool[MAX_GPU_PARTICLES];
static unsigned int    s_vbo             = 0;
static unsigned int    s_vao             = 0;
static Shader          s_draw_shader_cpu = {0};

static int             s_spawn_cursor    = 0;

// ---------------------------------------------------------------------------
// Compute shader loader
// ---------------------------------------------------------------------------
static unsigned int CompileComputeShader(const char *path) {
    char *src = LoadFileText(path);
    if (!src) {
        TraceLog(LOG_WARNING, "GPU_PARTICLES: cannot load %s", path);
        return 0;
    }

    unsigned int shader = s_glCreateShader(GL_COMPUTE_SHADER);
    s_glShaderSource(shader, 1, (const char **)&src, NULL);
    s_glCompileShader(shader);
    UnloadFileText(src);

    int ok = 0;
    s_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; int len;
        s_glGetShaderInfoLog(shader, 512, &len, log);
        TraceLog(LOG_ERROR, "GPU_PARTICLES compute compile: %s", log);
        s_glDeleteShader(shader);
        return 0;
    }

    unsigned int prog = s_glCreateProgram();
    s_glAttachShader(prog, shader);
    s_glLinkProgram(prog);
    s_glDeleteShader(shader);

    int link_ok = 0;
    s_glGetProgramiv(prog, GL_LINK_STATUS, &link_ok);
    if (!link_ok) {
        char log[512]; int len;
        s_glGetProgramInfoLog(prog, 512, &len, log);
        TraceLog(LOG_ERROR, "GPU_PARTICLES compute link: %s", log);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
// VBO layout: position(3f) + texcoord(2f) + color(4f) = 36 bytes/vert
// ---------------------------------------------------------------------------
#define CPU_VERT_STRIDE       36
#define CPU_VERTS_PER_PARTICLE 6
#define CPU_VBO_SIZE (MAX_GPU_PARTICLES * CPU_VERTS_PER_PARTICLE * CPU_VERT_STRIDE)

// Dùng rlgl wrappers — không cần GL headers platform-specific
static void SetupCpuVAO(void) {
    s_vao = rlLoadVertexArray();
    rlEnableVertexArray(s_vao);

    s_vbo = rlLoadVertexBuffer(NULL, CPU_VBO_SIZE, true);

    // Attrib 0: position vec3, offset 0
    rlEnableVertexAttribute(0);
    rlSetVertexAttribute(0, 3, RL_FLOAT, 0, CPU_VERT_STRIDE, 0);
    // Attrib 1: texcoord vec2, offset 12
    rlEnableVertexAttribute(1);
    rlSetVertexAttribute(1, 2, RL_FLOAT, 0, CPU_VERT_STRIDE, 12);
    // Attrib 2: color vec4, offset 20
    rlEnableVertexAttribute(2);
    rlSetVertexAttribute(2, 4, RL_FLOAT, 0, CPU_VERT_STRIDE, 20);

    rlDisableVertexArray();
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void GpuParticleSystem_Init(void) {
    if (s_initialized) return;

    memset(s_cpu_pool, 0, sizeof(s_cpu_pool));
    s_spawn_cursor = 0;

    // Thử load compute procs — nếu thành công → hardware hỗ trợ GL 4.3 / GLES 3.1
    bool gl43 = LoadComputeProcs();

    // Optional GL_VERSION logging (macOS s_LoadProc = NULL → bỏ qua)
    pfn_GetString p_GetStr = (pfn_GetString)s_LoadProc("glGetString");
    if (p_GetStr) {
        const char *ver = (const char *)p_GetStr(0x1F02); // GL_VERSION
        TraceLog(LOG_INFO, "GPU_PARTICLES: GL_VERSION = %s", ver ? ver : "?");
    }

    if (gl43) {
        // ----- COMPUTE PATH -----
        const char *comp_path    = "core/shaders/gpu_particles.comp";
        const char *ssbo_vs_path = "core/shaders/gpu_particles_ssbo.vs";
        const char *fs_path      = "core/shaders/gpu_particles.fs";

        s_compute_prog = CompileComputeShader(comp_path);
        if (!s_compute_prog) {
            TraceLog(LOG_WARNING, "GPU_PARTICLES: compute compile failed, fallback CPU");
            goto cpu_path;
        }

        // SSBO — dùng proc-loaded buffer ops, không cần raw GL headers
        unsigned int ssbo_buf[1];
        s_glGenBuffers(1, ssbo_buf);
        s_ssbo = ssbo_buf[0];
        s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_ssbo);
        s_glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_ssbo);
        s_glBufferData(GL_SHADER_STORAGE_BUFFER,
                       MAX_GPU_PARTICLES * (ptrdiff_t)sizeof(GpuParticleData),
                       NULL, RL_DYNAMIC_DRAW);
        s_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Empty VAO — vertex shader tự tính từ gl_VertexID
        s_draw_vao = rlLoadVertexArray();

        s_draw_shader_gpu = ResourceManager_LoadShader(ssbo_vs_path, fs_path);
        s_use_compute = true;
        TraceLog(LOG_INFO, "GPU_PARTICLES: COMPUTE path active (%d particles)", MAX_GPU_PARTICLES);
    } else {
        cpu_path:
        // ----- CPU/VBO PATH -----
        SetupCpuVAO();
        s_draw_shader_cpu = ResourceManager_LoadShader(
            "core/shaders/gpu_particles_vbo.vs",
            "core/shaders/gpu_particles.fs"
        );
        s_use_compute = false;
        TraceLog(LOG_INFO, "GPU_PARTICLES: CPU/VBO path active (%d particles)", MAX_GPU_PARTICLES);
    }

    s_initialized = true;
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------
void GpuParticleSystem_Spawn(GpuParticleConfig cfg) {
    if (!s_initialized) return;

    int idx = s_spawn_cursor % MAX_GPU_PARTICLES;
    s_spawn_cursor++;

    GpuParticleData d;
    d.px = cfg.position.x; d.py = cfg.position.y; d.pz = cfg.position.z;
    d.radius = cfg.radius;
    d.vx = cfg.velocity.x; d.vy = cfg.velocity.y; d.vz = cfg.velocity.z;
    d.drag = cfg.drag;
    d.csr = cfg.colorStart.r / 255.0f; d.csg = cfg.colorStart.g / 255.0f;
    d.csb = cfg.colorStart.b / 255.0f; d.csa = cfg.colorStart.a / 255.0f;
    d.cer = cfg.colorEnd.r / 255.0f;   d.ceg = cfg.colorEnd.g / 255.0f;
    d.ceb = cfg.colorEnd.b / 255.0f;   d.cea = cfg.colorEnd.a / 255.0f;
    d.life_rem = cfg.lifetime;
    d.life_max = cfg.lifetime;
    d.phase = (float)GetRandomValue(0, 10000) / 10000.0f;
    d.active = 1.0f;

    if (s_use_compute) {
        s_glBindBuffer(GL_SHADER_STORAGE_BUFFER, s_ssbo);
        s_glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                          idx * (ptrdiff_t)sizeof(GpuParticleData),
                          sizeof(GpuParticleData), &d);
        s_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    } else {
        s_cpu_pool[idx] = d;
    }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void GpuParticleSystem_Update(float dt) {
    if (!s_initialized) return;

    if (s_use_compute) {
        s_glUseProgram(s_compute_prog);
        int loc_dt = s_glGetUniformLocation(s_compute_prog, "u_dt");
        if (loc_dt >= 0) s_glUniform1f(loc_dt, dt);

        s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_ssbo);
        unsigned int groups = (MAX_GPU_PARTICLES + 255) / 256;
        s_glDispatchCompute(groups, 1, 1);
        s_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        s_glUseProgram(0);
    } else {
        for (int i = 0; i < MAX_GPU_PARTICLES; i++) {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f) continue;
            p->life_rem -= dt;
            if (p->life_rem <= 0.0f) { p->active = 0.0f; continue; }
            float drag_f = 1.0f - p->drag * dt;
            if (drag_f < 0.0f) drag_f = 0.0f;
            p->vx *= drag_f; p->vy *= drag_f; p->vz *= drag_f;
            p->px += p->vx * dt; p->py += p->vy * dt; p->pz += p->vz * dt;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture) {
    if (!s_initialized) return;

    Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera.position, camera.target));
    Vector3 right   = Vector3Normalize(Vector3CrossProduct(camera.up, viewDir));
    Vector3 up      = Vector3CrossProduct(viewDir, right);

    if (s_use_compute) {
        BeginShaderMode(s_draw_shader_gpu);

        int loc_right = GetShaderLocation(s_draw_shader_gpu, "u_right");
        int loc_up    = GetShaderLocation(s_draw_shader_gpu, "u_up");
        if (loc_right >= 0) SetShaderValue(s_draw_shader_gpu, loc_right, &right, SHADER_UNIFORM_VEC3);
        if (loc_up    >= 0) SetShaderValue(s_draw_shader_gpu, loc_up,    &up,    SHADER_UNIFORM_VEC3);

        s_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_ssbo);
        rlSetTexture(texture.id);

        rlEnableVertexArray(s_draw_vao);
        rlDrawVertexArray(0, MAX_GPU_PARTICLES * 6);
        rlDisableVertexArray();

        rlSetTexture(0);
        EndShaderMode();

    } else {
        static float vbo_data[MAX_GPU_PARTICLES * CPU_VERTS_PER_PARTICLE * 9];
        int vert_count = 0;

        for (int i = 0; i < MAX_GPU_PARTICLES; i++) {
            GpuParticleData *p = &s_cpu_pool[i];
            if (p->active < 0.5f) continue;

            float t = 1.0f - (p->life_rem / p->life_max);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float cr = p->csr + (p->cer - p->csr) * t;
            float cg = p->csg + (p->ceg - p->csg) * t;
            float cb = p->csb + (p->ceb - p->csb) * t;
            float ca = p->csa + (p->cea - p->csa) * t;
            float cx = p->px, cy = p->py, cz = p->pz, r = p->radius;

            float tl[3] = { cx + (-right.x - up.x)*r, cy + (-right.y - up.y)*r, cz + (-right.z - up.z)*r };
            float tr[3] = { cx + ( right.x - up.x)*r, cy + ( right.y - up.y)*r, cz + ( right.z - up.z)*r };
            float bl[3] = { cx + (-right.x + up.x)*r, cy + (-right.y + up.y)*r, cz + (-right.z + up.z)*r };
            float br[3] = { cx + ( right.x + up.x)*r, cy + ( right.y + up.y)*r, cz + ( right.z + up.z)*r };

            if (vert_count + 6 > MAX_GPU_PARTICLES * CPU_VERTS_PER_PARTICLE) break;

            float *v = &vbo_data[vert_count * 9];
            v[0]=tl[0];v[1]=tl[1];v[2]=tl[2];v[3]=0.f;v[4]=0.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;v+=9;
            v[0]=bl[0];v[1]=bl[1];v[2]=bl[2];v[3]=0.f;v[4]=1.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;v+=9;
            v[0]=br[0];v[1]=br[1];v[2]=br[2];v[3]=1.f;v[4]=1.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;v+=9;
            v[0]=tl[0];v[1]=tl[1];v[2]=tl[2];v[3]=0.f;v[4]=0.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;v+=9;
            v[0]=br[0];v[1]=br[1];v[2]=br[2];v[3]=1.f;v[4]=1.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;v+=9;
            v[0]=tr[0];v[1]=tr[1];v[2]=tr[2];v[3]=1.f;v[4]=0.f;v[5]=cr;v[6]=cg;v[7]=cb;v[8]=ca;

            vert_count += 6;
        }

        if (vert_count == 0) return;

        rlUpdateVertexBuffer(s_vbo, vbo_data, vert_count * 9 * (int)sizeof(float), 0);

        BeginShaderMode(s_draw_shader_cpu);
        rlSetTexture(texture.id);
        rlEnableVertexArray(s_vao);
        rlDrawVertexArray(0, vert_count);
        rlDisableVertexArray();
        rlSetTexture(0);
        EndShaderMode();
    }
}

// ---------------------------------------------------------------------------
// Unload
// ---------------------------------------------------------------------------
void GpuParticleSystem_Unload(void) {
    if (!s_initialized) return;
    if (s_use_compute) {
        if (s_ssbo)     { rlUnloadVertexBuffer(s_ssbo); s_ssbo = 0; }
        if (s_draw_vao) { rlUnloadVertexArray(s_draw_vao); s_draw_vao = 0; }
        if (s_compute_prog && s_glUseProgram) {
            s_glUseProgram(0);
            rlUnloadShaderProgram(s_compute_prog);
            s_compute_prog = 0;
        }
    } else {
        if (s_vbo) { rlUnloadVertexBuffer(s_vbo); s_vbo = 0; }
        if (s_vao) { rlUnloadVertexArray(s_vao); s_vao = 0; }
    }
    s_initialized = false;
    s_use_compute = false;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------
bool GpuParticleSystem_IsComputeActive(void) { return s_use_compute; }

int GpuParticleSystem_ActiveCount(void) {
    if (!s_initialized) return 0;
    if (s_use_compute) return s_spawn_cursor < MAX_GPU_PARTICLES ? s_spawn_cursor : MAX_GPU_PARTICLES;
    int count = 0;
    for (int i = 0; i < MAX_GPU_PARTICLES; i++) {
        if (s_cpu_pool[i].active >= 0.5f) count++;
    }
    return count;
}

void GpuParticleSystem_DrawDebug(int x, int y) {
    if (!s_initialized) {
        DrawText("GpuParticles: NOT INIT", x, y, 18, RED);
        return;
    }

    const char *path_name  = s_use_compute ? "COMPUTE (GPU)" : "CPU / VBO";
    Color        path_color = s_use_compute ? GREEN : YELLOW;

    // rlGetVersion(): 3=GL3.3, 4=GL4.3, 5=GLES2, 6=GLES3
    int rl_ver = rlGetVersion();
    const char *ver_label =
        rl_ver == 4 ? "GL 4.3+" :
        rl_ver == 3 ? "GL 3.3"  :
        rl_ver == 5 ? "GLES 2.0" :
        rl_ver == 6 ? "GLES 3.x" : "GL ?";

    int active = GpuParticleSystem_ActiveCount();

    DrawText(TextFormat("GpuParticles  [%s]", path_name),                   x, y,      18, path_color);
    DrawText(TextFormat("API: %s", ver_label),                               x, y + 22, 14, LIGHTGRAY);
    DrawText(TextFormat("Pool: %d / %d active", active, MAX_GPU_PARTICLES), x, y + 40, 14, LIGHTGRAY);
}
