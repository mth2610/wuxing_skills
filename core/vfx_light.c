#include "vfx_light.h"
#include <stddef.h> // NULL — VFXLight_SlotFor returns it on a full cache
#include "core/tuning.h"
#include "rlgl.h"    // rlGetMatrixTransform — see VFXLight_ShaderSpaceMatrix
#include "raymath.h" // Vector3Transform

typedef struct {
    Vector3 position;
    Color color;
    float radius;
    float lifetime;
    float maxLifetime;
    VFXPriority priority;
    bool active;
} VFXLightInternal;

// Quản lý bằng bộ nhớ tĩnh (No-malloc) theo đúng triết lý của project
static VFXLightInternal g_VFXLights[MAX_VFX_LIGHTS];

void VFXLight_Init(void) {
    VFXLight_Reset();
}

void VFXLight_Reset(void) {
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        g_VFXLights[i].active = false;
    }
}

void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXPriority priority) {
    int freeIdx = -1;
    int evictIdx = -1;
    VFXPriority evictPriority = VFX_PRIORITY_HIGH_ULTIMATE;
    float evictLifetime = 999999.0f;

    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        if (!g_VFXLights[i].active) {
            freeIdx = i;
            break;
        }
        // Track the lowest-priority slot; among equal priority, the one with
        // the shortest remaining lifetime (about to expire anyway).
        if (evictIdx == -1 || g_VFXLights[i].priority < evictPriority ||
            (g_VFXLights[i].priority == evictPriority &&
             g_VFXLights[i].lifetime < evictLifetime)) {
            evictIdx = i;
            evictPriority = g_VFXLights[i].priority;
            evictLifetime = g_VFXLights[i].lifetime;
        }
    }

    int targetIdx = -1;
    if (freeIdx != -1) {
        targetIdx = freeIdx;
    } else if (evictIdx != -1 && evictPriority <= priority) {
        // Only evict a slot whose priority is <= the incoming spawn's — a
        // low-priority spawn must not evict an existing higher-priority one.
        targetIdx = evictIdx;
    }

    if (targetIdx != -1) {
        g_VFXLights[targetIdx].position = pos;
        g_VFXLights[targetIdx].color = color;
        g_VFXLights[targetIdx].radius = radius;
        g_VFXLights[targetIdx].lifetime = lifetime;
        g_VFXLights[targetIdx].maxLifetime = lifetime;
        g_VFXLights[targetIdx].priority = priority;
        g_VFXLights[targetIdx].active = true;
    }
}

void VFXLight_Update(float dt) {
    for (int i = 0; i < MAX_VFX_LIGHTS; i++) {
        if (!g_VFXLights[i].active) continue;

        g_VFXLights[i].lifetime -= dt;
        if (g_VFXLights[i].lifetime <= 0.0f) {
            g_VFXLights[i].active = false;
        }
    }
}

void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount) {
    int activeCount = 0;
    int limit = (maxCount < MAX_VFX_LIGHTS) ? maxCount : MAX_VFX_LIGHTS;

    for (int i = 0; i < MAX_VFX_LIGHTS && activeCount < limit; i++) {
        if (g_VFXLights[i].active) {
            out[activeCount].position = g_VFXLights[i].position;
            out[activeCount].radius = g_VFXLights[i].radius;
            
            // Có thể thực hiện tính toán fade-out màu sắc dựa trên tỷ lệ lifetime còn lại tại đây
            float alphaRatio = g_VFXLights[i].lifetime / g_VFXLights[i].maxLifetime;
            Color finalColor = g_VFXLights[i].color;
            finalColor.a = (unsigned char)(finalColor.a * alphaRatio);
            
            out[activeCount].color = finalColor;
            activeCount++;
        }
    }
    *count = activeCount;
}
// Either pointer may be NULL — callers routinely want only one of the two.
// Writing through both unconditionally segfaulted the moment anything asked for
// just the active count, which is exactly what particle_system.c's perf log
// does: turning `particle_perf_log` on crashed the game rather than printing a
// line, so the one instrument built for diagnosing particle cost could not be
// switched on. An out-param that is documented as optional must be checked.
void VFXLight_GetStats(int *active, int *max) {
    int n = 0;
    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
        if (g_VFXLights[i].active) n++;
    if (active) *active = n;
    if (max) *max = MAX_VFX_LIGHTS;
}

// ── Đợt E / E2 — bind the pool to any lit shader ─────────────────────────────
//
// Locations are cached per shader id. A tiny fixed table rather than a lookup
// per frame: there are a handful of lit shaders (character, ground, props), and
// GetShaderLocation is a driver round-trip that has no business running 60 times
// a second per uniform.
#define VFXLIGHT_SHADER_CACHE 8

// The SHADER-side array size, which is NOT MAX_VFX_LIGHTS. That one is the POOL
// size (16); the uniform arrays declared in vfx_lights.glsl hold 4. Uploading
// pool-many entries into a 4-element uniform array overruns it — the two numbers
// look interchangeable and are not. Keep this in step with the .glsl #define.
#define VFXLIGHT_SHADER_MAX 4

typedef struct {
    unsigned int id;
    int locCount, locPos, locColor, locGain, locDebug;
} VFXLightShaderSlot;

static VFXLightShaderSlot s_shaderCache[VFXLIGHT_SHADER_CACHE];
static int s_shaderCacheCount = 0;

static VFXLightShaderSlot *VFXLight_SlotFor(Shader shader)
{
    for (int i = 0; i < s_shaderCacheCount; i++)
        if (s_shaderCache[i].id == shader.id) return &s_shaderCache[i];

    if (s_shaderCacheCount >= VFXLIGHT_SHADER_CACHE)
    {
        // Never fail silently: a shader dropped off the end would simply stop
        // being lit, with nothing on screen to say why.
        TraceLog(LOG_WARNING,
                 "VFXLIGHT: shader cache full (%d) — shader %u will not receive "
                 "VFX lights. Raise VFXLIGHT_SHADER_CACHE.",
                 VFXLIGHT_SHADER_CACHE, shader.id);
        return NULL;
    }

    VFXLightShaderSlot *s = &s_shaderCache[s_shaderCacheCount++];
    s->id        = shader.id;
    s->locCount  = GetShaderLocation(shader, "u_vfxLightCount");
    s->locPos    = GetShaderLocation(shader, "u_vfxLightPosRadius");
    s->locColor  = GetShaderLocation(shader, "u_vfxLightColor");
    s->locGain   = GetShaderLocation(shader, "u_vfxLightGain");
    s->locDebug  = GetShaderLocation(shader, "u_vfxLightDebug");
    // One line per shader, on first bind. Without it, "this surface was never
    // wired" and "it was wired and the light is out of range" look identical on
    // screen — and the first of those is a five-minute fix aimed at the wrong
    // file if you cannot tell them apart.
    if (s->locCount < 0)
        TraceLog(LOG_WARNING,
                 "VFXLIGHT: shader %u has NO u_vfxLightCount — missing #include "
                 "core/shaders/common/vfx_lights.glsl, or loaded with raw "
                 "LoadShader instead of ResourceManager_LoadShader (the "
                 "preprocessor is what resolves #include).", shader.id);
    else
        TraceLog(LOG_INFO, "VFXLIGHT: shader %u now receives VFX lights", shader.id);
    return s;
}

// THE SPACE A LIT SURFACE ACTUALLY COMPUTES IN — read this before touching the
// upload, it is the entire reason E2 lit nothing for two sessions.
//
// Every consumer does `fragPosition = matModel * vertexPosition` and calls the
// result world space. It is NOT. raylib's DrawMesh builds
//     matModel = modelTransform * rlGetMatrixTransform()
// and `rlGetMatrixTransform()` returns RLGL.State.transform, which main.c's
// MyBeginMode3D fills with the VIEW MATRIX: rlPushMatrix() in RL_MODELVIEW mode
// flips rlgl into "transformRequired" and redirects the current matrix to
// State.transform, so the rlLoadIdentity + rlMultMatrixf(matView) that follow
// land there. Measured, not deduced — rlGetMatrixTransform()'s translation row
// came back bit-identical to MatrixLookAt(camera)'s.
//
// So every lit surface's "world" position is a VIEW-space position, and a light
// uploaded in world space is tens of metres away from the fragment sitting right
// underneath it: attenuation clamps to 0 and NOTHING is ever lit, on any surface,
// at any tier. That is the whole bug, and it is invisible to fract(worldPos) —
// a view-space position paints exactly the same convincing 1 m grid, which is
// why the debug view said the coordinates were fine.
//
// Rather than teach three vertex shaders to undo it, the lights are moved INTO
// the same space here. `maps/toolkit/ground_shadow.c` solves the identical trap
// the same way (it folds inverse(rlGetMatrixTransform()) into its light matrix).
// Normals need no correction: DrawMesh derives matNormal from the same matModel,
// so N is view space too and dot(N, toLight) stays consistent.
static Matrix VFXLight_ShaderSpaceMatrix(void)
{
    Matrix m = rlGetMatrixTransform();
    // Identity means we are OUTSIDE the 3D pass — the view has not been pushed
    // yet, the conversion would be a no-op, and every light would silently land
    // in the wrong space again. Say so instead of rendering nothing.
    if (m.m12 == 0.0f && m.m13 == 0.0f && m.m14 == 0.0f &&
        m.m0 == 1.0f && m.m5 == 1.0f && m.m10 == 1.0f)
    {
        static bool s_warned = false;
        if (!s_warned)
        {
            s_warned = true;
            TraceLog(LOG_WARNING,
                     "VFXLIGHT: BindAll ran OUTSIDE the 3D pass (rlGetMatrixTransform "
                     "is identity) — lights will be uploaded in world space while "
                     "surfaces compute in view space, and nothing will light. Move "
                     "the call after MyBeginMode3D.");
        }
    }
    return m;
}

void VFXLight_BindToShader(Shader shader, int maxLights)
{
    VFXLightShaderSlot *slot = VFXLight_SlotFor(shader);
    if (!slot || slot->locCount < 0) return;

    if (maxLights < 0) maxLights = 0;
    if (maxLights > VFXLIGHT_SHADER_MAX) maxLights = VFXLIGHT_SHADER_MAX;

    VFXLightData lights[VFXLIGHT_SHADER_MAX];
    int count = 0;
    if (maxLights > 0) VFXLight_GetActive(lights, &count, maxLights);
    if (count > maxLights) count = maxLights;

    // vec4 arrays — see the std140 note in vfx_lights.glsl. Radius rides in .w.
    float pos[VFXLIGHT_SHADER_MAX * 4] = {0};
    float col[VFXLIGHT_SHADER_MAX * 4] = {0};
    Matrix toShaderSpace = VFXLight_ShaderSpaceMatrix();
    for (int i = 0; i < count; i++)
    {
        // World -> the space the surfaces actually compute in (view). Radius is
        // a length and the view matrix is rigid, so it needs no conversion.
        Vector3 p = Vector3Transform(lights[i].position, toShaderSpace);
        pos[i * 4 + 0] = p.x;
        pos[i * 4 + 1] = p.y;
        pos[i * 4 + 2] = p.z;
        pos[i * 4 + 3] = lights[i].radius;
        // Alpha IS the intensity — VFXLight_GetActive already fades it with the
        // light's remaining lifetime, so folding it into the colour gives the
        // fade-out for free. Ignoring it (the first version did) meant lights
        // popped out at full brightness.
        float inten = lights[i].color.a / 255.0f;
        col[i * 4 + 0] = lights[i].color.r / 255.0f * inten;
        col[i * 4 + 1] = lights[i].color.g / 255.0f * inten;
        col[i * 4 + 2] = lights[i].color.b / 255.0f * inten;
        col[i * 4 + 3] = 1.0f;
    }

    static float s_gain = 3.0f;
    static bool s_gainReg = false;
    if (!s_gainReg) { s_gainReg = true; Tuning_RegisterFloat("vfx_light_gain", &s_gain, 3.0f); }
    if (slot->locGain >= 0)
        SetShaderValue(shader, slot->locGain, &s_gain, SHADER_UNIFORM_FLOAT);

    static float s_dbgMode = 0.0f;
    static bool s_dbgModeReg = false;
    if (!s_dbgModeReg) { s_dbgModeReg = true; Tuning_RegisterFloat("vfx_light_debug_mode", &s_dbgMode, 0.0f); }
    if (slot->locDebug >= 0)
        SetShaderValue(shader, slot->locDebug, &s_dbgMode, SHADER_UNIFORM_FLOAT);

    // The numbers as uploaded, once a second, while any debug view is on. Every
    // round of this bug was an argument about what the shader receives; one line
    // of what the C side actually sends ends it — and the light's position here
    // is post-conversion, so a wrong SPACE shows up as a coordinate that has no
    // business being there. Rate-limited, not edge-triggered: the values move
    // every frame, and "it changed" is not the question.
    if (s_dbgMode > 0.5f)
    {
        static double s_lastLog = -1.0;
        double now = GetTime();
        if (now - s_lastLog > 1.0)
        {
            s_lastLog = now;
            TraceLog(LOG_INFO, "VFXLIGHT: upload sh=%u count=%d gain=%.2f dbg=%.1f "
                               "L0 pos=(%.2f,%.2f,%.2f) r=%.2f col=(%.2f,%.2f,%.2f)",
                     shader.id, count, (double)s_gain, (double)s_dbgMode,
                     (double)pos[0], (double)pos[1], (double)pos[2], (double)pos[3],
                     (double)col[0], (double)col[1], (double)col[2]);
        }
    }

    SetShaderValue(shader, slot->locCount, &count, SHADER_UNIFORM_INT);
    SetShaderValueV(shader, slot->locPos, pos, SHADER_UNIFORM_VEC4, VFXLIGHT_SHADER_MAX);
    SetShaderValueV(shader, slot->locColor, col, SHADER_UNIFORM_VEC4, VFXLIGHT_SHADER_MAX);
}

void VFXLight_RegisterShader(Shader shader)
{
    if (shader.id == 0) return;
    VFXLight_SlotFor(shader);   // caches locations + logs whether it qualifies
}

void VFXLight_BindAll(int maxLights)
{
    // Report the COUNT, on change. "No light on screen" has two completely
    // different causes — nothing in the pool, or lights present but too weak /
    // out of range — and they are indistinguishable by looking. Guessing between
    // them has already cost several rounds this session.
    {
        // Only on the 0 <-> non-zero edge. Logging every count change spammed
        // hundreds of lines a second (a fire refreshing its light oscillates
        // 1<->2 forever), which buries everything else in the console.
        static int lastHad = -1;
        int active = 0, poolMax = 0;
        VFXLight_GetStats(&active, &poolMax);
        int had = active > 0 ? 1 : 0;
        if (had != lastHad)
        {
            lastHad = had;
            TraceLog(LOG_INFO, "VFXLIGHT: %d active in pool (of %d), uploading up to %d "
                               "to %d registered shader(s)",
                     active, poolMax, maxLights, s_shaderCacheCount);
        }
    }

    for (int i = 0; i < s_shaderCacheCount; i++)
    {
        if (s_shaderCache[i].locCount < 0) continue;   // shader lacks the block
        Shader sh = {0};
        sh.id = s_shaderCache[i].id;
        VFXLight_BindToShader(sh, maxLights);
    }
}

// A light you control from tuning.cfg, parked wherever the caller says (main.c
// passes the player). It exists because every "the lights don't light anything"
// round so far was really "the skill's light was 0.65 m wide, lived 0.3 s, and
// was behind a rock" — a question about WHERE the light was, not about whether
// lighting works. With this on, the answer is one glance:
//   vfx_light_test        = 1     spawn it
//   vfx_light_test_radius = 6.0   metres
// No pool at all -> the surface is not wired. A pool and no light -> magnitude.
void VFXLight_DebugTestLight(Vector3 pos)
{
    static float s_on = 0.0f, s_radius = 6.0f;
    static bool s_reg = false;
    if (!s_reg)
    {
        s_reg = true;
        Tuning_RegisterFloat("vfx_light_test", &s_on, 0.0f);
        Tuning_RegisterFloat("vfx_light_test_radius", &s_radius, 6.0f);
    }
    if (s_on <= 0.5f) return;
    // Re-spawned every frame with a lifetime just over one frame: the pool's own
    // expiry keeps it from accumulating, and it follows the caller for free.
    VFXLight_Spawn((Vector3){pos.x, pos.y + 1.0f, pos.z}, (Color){255, 150, 60, 255},
                   s_radius, 0.05f, VFX_PRIORITY_HIGH_ULTIMATE);
}

void VFXLight_DrawDebug(void)
{
    static float s_dbg = 0.0f;
    static bool s_dbgReg = false;
    if (!s_dbgReg) { s_dbgReg = true; Tuning_RegisterFloat("vfx_light_debug", &s_dbg, 0.0f); }
    if (s_dbg <= 0.5f) return;

    for (int i = 0; i < MAX_VFX_LIGHTS; i++)
    {
        if (!g_VFXLights[i].active) continue;
        Vector3 p = g_VFXLights[i].position;
        Color c = g_VFXLights[i].color;
        c.a = 255;
        // Small solid marker at the exact position, plus the radius sphere.
        DrawSphere(p, 0.08f, c);
        DrawSphereWires(p, g_VFXLights[i].radius, 8, 8, c);
        // Vertical stick to the ground plane, so a light floating above or below
        // the surface is unmistakable.
        DrawLine3D(p, (Vector3){p.x, 0.0f, p.z}, c);
    }
}
