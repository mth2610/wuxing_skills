#include "core/lightning/lightning_stroke.h"

#include "core/resource_manager.h"
#include "core/ribbon_strip.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>

#define LIGHTNING_STROKE_MAX             32
#define LIGHTNING_STROKE_MAX_POINTS RIBBON_MIDPOINT_MAX_POINTS
#define LIGHTNING_STROKE_MAX_BRANCHES      2
#define LIGHTNING_STROKE_BRANCH_POINTS     9
#define LIGHTNING_STROKE_MIN_LENGTH    0.05f

typedef struct {
    bool active;
    int serial;
    Vector3 from, to;
    LightningStrokeConfig config;
    float elapsed;
    float nextFlicker;
    unsigned int seed;
    Vector3 points[LIGHTNING_STROKE_MAX_POINTS];
    int pointCount;
    Vector3 branches[LIGHTNING_STROKE_MAX_BRANCHES][LIGHTNING_STROKE_BRANCH_POINTS];
    int branchCount;
} LightningStroke;

typedef struct {
    Shader shader;
    int phase;
    int mode;
    int bodyColor;
    int haloColor;
    int coreColor;
    int lineWidth;
    int travel;
    int lifeFade;
    int coreEmission;
    int haloEmission;
    bool tried;
} LightningStrokeShader;

static LightningStroke s_strokes[LIGHTNING_STROKE_MAX];
static int s_strokeSerial = 0;
static LightningStrokeShader s_shader;

static float LightningStroke_SmoothStep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static unsigned int LightningStroke_Hash(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static unsigned int LightningStroke_DeriveSeed(Vector3 from, Vector3 to)
{
    unsigned int x = (unsigned int)fabsf(from.x * 73856093.0f);
    unsigned int y = (unsigned int)fabsf(from.y * 19349663.0f);
    unsigned int z = (unsigned int)fabsf(from.z * 83492791.0f);
    unsigned int tx = (unsigned int)fabsf(to.x * 2654435761.0f);
    unsigned int tz = (unsigned int)fabsf(to.z * 2246822519.0f);
    return LightningStroke_Hash(x ^ y ^ z ^ tx ^ tz);
}

static float LightningStroke_Random01(unsigned int seed)
{
    return (float)(LightningStroke_Hash(seed) & 0x00ffffffu) / 16777215.0f;
}

static void LightningStroke_InitShader(void)
{
    if (s_shader.tried) return;
    s_shader.tried = true;
    s_shader.shader = ResourceManager_LoadShader(
        "core/lightning/shaders/lightning_stroke.vs",
        "core/lightning/shaders/lightning_stroke.fs");
    if (s_shader.shader.id == 0) return;
    s_shader.phase = GetShaderLocation(s_shader.shader, "u_phase");
    s_shader.mode = GetShaderLocation(s_shader.shader, "u_mode");
    s_shader.bodyColor = GetShaderLocation(s_shader.shader, "u_bodyColor");
    s_shader.haloColor = GetShaderLocation(s_shader.shader, "u_haloColor");
    s_shader.coreColor = GetShaderLocation(s_shader.shader, "u_coreColor");
    s_shader.lineWidth = GetShaderLocation(s_shader.shader, "u_lineWidth");
    s_shader.travel = GetShaderLocation(s_shader.shader, "u_travel");
    s_shader.lifeFade = GetShaderLocation(s_shader.shader, "u_lifeFade");
    s_shader.coreEmission = GetShaderLocation(s_shader.shader, "u_coreEmission");
    s_shader.haloEmission = GetShaderLocation(s_shader.shader, "u_haloEmission");
}

static bool LightningStroke_HasShader(void)
{
    return s_shader.shader.id != 0 && s_shader.phase >= 0 && s_shader.mode >= 0 &&
           s_shader.bodyColor >= 0 && s_shader.haloColor >= 0 &&
           s_shader.coreColor >= 0 && s_shader.lineWidth >= 0 && s_shader.travel >= 0 &&
           s_shader.lifeFade >= 0 &&
           s_shader.coreEmission >= 0 && s_shader.haloEmission >= 0;
}

static void LightningStroke_BuildPath(LightningStroke *stroke)
{
    float length = Vector3Distance(stroke->from, stroke->to);
    if (length < LIGHTNING_STROKE_MIN_LENGTH) {
        stroke->pointCount = 0;
        return;
    }

    // The primary filament is now GPU domain-warped on one endpoint-pinned
    // sheet. Keep this compact midpoint path for optional branch synthesis and
    // for deterministic Core consumers of the shared path primitive.
    RibbonMidpointConfig path = Ribbon_MidpointDefaultConfig();
    path.levels = length > 3.5f ? 4 : 3;
    path.amplitudeDecay = 0.52f;
    float maximumOffset = fminf(stroke->config.jaggedness, length * 0.14f);
    path.initialAmplitude = maximumOffset * (1.0f - path.amplitudeDecay);
    path.seed = LightningStroke_Hash(stroke->seed +
        (unsigned int)(stroke->elapsed * 1000.0f));
    stroke->pointCount = Ribbon_GenerateMidpointDisplacement(
        stroke->from, stroke->to, &path, stroke->points,
        LIGHTNING_STROKE_MAX_POINTS);
    stroke->branchCount = 0;
    int requestedBranches = stroke->config.branchCount;
    if (requestedBranches < 0) requestedBranches = 0;
    if (requestedBranches > LIGHTNING_STROKE_MAX_BRANCHES)
        requestedBranches = LIGHTNING_STROKE_MAX_BRANCHES;
    for (int b = 0; b < requestedBranches && stroke->pointCount > 3; ++b) {
        unsigned int branchSeed = path.seed + (unsigned int)b * 0x9e3779b9u;
        int origin = 1 + (int)(LightningStroke_Random01(branchSeed) *
                               (float)(stroke->pointCount - 3));
        Vector3 root = stroke->points[origin];
        Vector3 tangent = Vector3Normalize(Vector3Subtract(stroke->points[origin + 1], root));
        Vector3 ref = fabsf(tangent.y) < 0.92f ? (Vector3){0.0f, 1.0f, 0.0f}
                                                : (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 side = Vector3Normalize(Vector3CrossProduct(tangent, ref));
        float branchLength = maximumOffset * (0.28f +
            LightningStroke_Random01(branchSeed ^ 0x85ebca6bu) * 0.18f);
        Vector3 endpoint = Vector3Add(root, Vector3Scale(side,
            (b & 1) ? -branchLength : branchLength));
        RibbonMidpointConfig branch = path;
        branch.levels = 3;
        branch.initialAmplitude = branchLength * 0.18f;
        branch.seed = branchSeed;
        if (Ribbon_GenerateMidpointDisplacement(root, endpoint, &branch,
                stroke->branches[stroke->branchCount], LIGHTNING_STROKE_BRANCH_POINTS)
            == LIGHTNING_STROKE_BRANCH_POINTS)
            ++stroke->branchCount;
    }
}

static void LightningStroke_DrawWarpedSheet(const LightningStroke *stroke,
                                            Camera3D camera)
{
    Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    Vector3 segment = Vector3Subtract(stroke->to, stroke->from);
    float length = Vector3Length(segment);
    if (length < LIGHTNING_STROKE_MIN_LENGTH) { rlEnd(); return; }
    Vector3 tangent = Vector3Scale(segment, 1.0f / length);
    Vector3 side = Vector3CrossProduct(tangent, viewDir);
    if (Vector3Length(side) < 0.001f) side = Vector3CrossProduct(tangent, camera.up);
    if (Vector3Length(side) < 0.001f) side = Vector3CrossProduct(tangent, (Vector3){0.0f, 1.0f, 0.0f});
    side = Vector3Normalize(side);

    // A broad canvas is not a broad visible ribbon: the fragment shader carves
    // one warped distance-field filament from it, pinning that filament to the
    // source and target. This directly adapts the ShaderToy method to 3D.
    float canvasHalfWidth = fminf(stroke->config.jaggedness * 1.55f + stroke->config.width * 2.0f,
                                  length * 0.30f);
    if (canvasHalfWidth < stroke->config.width * 2.0f)
        canvasHalfWidth = stroke->config.width * 2.0f;
    float lineWidth = stroke->config.width / canvasHalfWidth;
    if (LightningStroke_HasShader())
        SetShaderValue(s_shader.shader, s_shader.lineWidth, &lineWidth, SHADER_UNIFORM_FLOAT);
    Vector3 leftFrom = Vector3Subtract(stroke->from, Vector3Scale(side, canvasHalfWidth));
    Vector3 rightFrom = Vector3Add(stroke->from, Vector3Scale(side, canvasHalfWidth));
    Vector3 leftTo = Vector3Subtract(stroke->to, Vector3Scale(side, canvasHalfWidth));
    Vector3 rightTo = Vector3Add(stroke->to, Vector3Scale(side, canvasHalfWidth));
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(leftFrom.x, leftFrom.y, leftFrom.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(rightFrom.x, rightFrom.y, rightFrom.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(rightTo.x, rightTo.y, rightTo.z);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(leftTo.x, leftTo.y, leftTo.z);
    rlEnd();
}

LightningStrokeConfig LightningStroke_DefaultConfig(void)
{
    return (LightningStrokeConfig){
        .bodyColor = (Color){42, 112, 255, 72},
        .haloColor = (Color){18, 92, 255, 68},
        .coreColor = (Color){200, 239, 255, 255},
        .width = 0.075f,
        .lifetime = 0.40f,
        .travelDuration = 0.10f,
        .postImpactDuration = 0.30f,
        .coreEmission = 4.5f,
        .haloEmission = 0.42f,
        .jaggedness = 0.80f,
        .flickerInterval = 0.045f,
        .branchCount = 0,
        .seed = 0u
    };
}

int LightningStroke_Spawn(Vector3 from, Vector3 to, const LightningStrokeConfig *config)
{
    LightningStrokeConfig resolved = config ? *config : LightningStroke_DefaultConfig();
    if (resolved.width <= 0.0f) resolved.width = 0.075f;
    if (resolved.travelDuration <= 0.0f) resolved.travelDuration = 0.10f;
    if (resolved.postImpactDuration >= 0.0f)
        resolved.lifetime = resolved.travelDuration + resolved.postImpactDuration;
    else {
        if (resolved.lifetime <= 0.0f) resolved.lifetime = 0.40f;
        if (resolved.travelDuration >= resolved.lifetime)
            resolved.travelDuration = resolved.lifetime * 0.75f;
    }
    if (resolved.coreEmission <= 0.0f) resolved.coreEmission = 4.5f;
    if (resolved.haloEmission <= 0.0f) resolved.haloEmission = 0.32f;
    if (resolved.jaggedness < 0.0f) resolved.jaggedness = 0.0f;
    if (resolved.flickerInterval < 0.016f) resolved.flickerInterval = 0.016f;

    int slot = -1;
    for (int i = 0; i < LIGHTNING_STROKE_MAX; ++i)
        if (!s_strokes[i].active) { slot = i; break; }
    if (slot < 0) {
        float oldest = -1.0f;
        for (int i = 0; i < LIGHTNING_STROKE_MAX; ++i)
            if (s_strokes[i].elapsed > oldest) { oldest = s_strokes[i].elapsed; slot = i; }
    }

    LightningStroke *stroke = &s_strokes[slot];
    *stroke = (LightningStroke){0};
    stroke->active = true;
    stroke->serial = ++s_strokeSerial;
    stroke->from = from;
    stroke->to = to;
    stroke->config = resolved;
    stroke->seed = resolved.seed ? resolved.seed : LightningStroke_DeriveSeed(from, to);
    LightningStroke_BuildPath(stroke);
    return (stroke->serial << 8) | slot;
}

void LightningStroke_SetEndpoints(int handle, Vector3 from, Vector3 to)
{
    int slot = handle & 0xff;
    if (handle < 0 || slot >= LIGHTNING_STROKE_MAX) return;
    LightningStroke *stroke = &s_strokes[slot];
    if (!stroke->active || (handle >> 8) != stroke->serial) return;
    stroke->from = from;
    stroke->to = to;
    LightningStroke_BuildPath(stroke);
}

void LightningStroke_Kill(int handle)
{
    int slot = handle & 0xff;
    if (handle < 0 || slot >= LIGHTNING_STROKE_MAX) return;
    LightningStroke *stroke = &s_strokes[slot];
    if (stroke->active && (handle >> 8) == stroke->serial) stroke->active = false;
}

void LightningStroke_Update(float dt)
{
    for (int i = 0; i < LIGHTNING_STROKE_MAX; ++i) {
        LightningStroke *stroke = &s_strokes[i];
        if (!stroke->active) continue;
        stroke->elapsed += dt;
        if (stroke->elapsed >= stroke->config.lifetime) {
            stroke->active = false;
            continue;
        }
        if (stroke->elapsed >= stroke->nextFlicker) {
            LightningStroke_BuildPath(stroke);
            stroke->nextFlicker += stroke->config.flickerInterval;
        }
    }
}

void LightningStroke_DrawLayer(Camera3D camera, LightningStrokeRenderLayer layer)
{
    LightningStroke_InitShader();
    bool shaded = LightningStroke_HasShader();
    // The camera-facing canvas is a two-sided transparent sheet, never a
    // closed mesh. Keep culling scoped off or an opposite winding can vanish.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    if (shaded) SkillManager_BeginShader(s_shader.shader);
    for (int i = 0; i < LIGHTNING_STROKE_MAX; ++i) {
        const LightningStroke *stroke = &s_strokes[i];
        if (!stroke->active || stroke->pointCount < 2) continue;
        float settledTime = fmaxf(stroke->elapsed - stroke->config.travelDuration, 0.0f);
        float phase = (float)(stroke->seed & 0xffffu) * 0.001f +
                      fminf(stroke->elapsed, stroke->config.travelDuration) * 3.0f +
                      settledTime * 9.0f;
        float travel = fminf(stroke->elapsed / stroke->config.travelDuration, 1.0f);
        float lifeFade = 1.0f;
        if (stroke->config.postImpactDuration > 0.0f) {
            float hold01 = settledTime / stroke->config.postImpactDuration;
            lifeFade = 1.0f - LightningStroke_SmoothStep(0.58f, 1.0f, hold01);
        }
        float shaderMode = layer == LIGHTNING_STROKE_RENDER_BODY ? 0.0f : 1.0f;
        if (shaded) {
            Vector4 bodyColor = ColorNormalize(stroke->config.bodyColor);
            Vector4 haloColor = ColorNormalize(stroke->config.haloColor);
            Vector4 coreColor = ColorNormalize(stroke->config.coreColor);
            SetShaderValue(s_shader.shader, s_shader.phase, &phase, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_shader.shader, s_shader.mode, &shaderMode, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_shader.shader, s_shader.bodyColor, &bodyColor, SHADER_UNIFORM_VEC4);
            SetShaderValue(s_shader.shader, s_shader.haloColor, &haloColor, SHADER_UNIFORM_VEC4);
            SetShaderValue(s_shader.shader, s_shader.coreColor, &coreColor, SHADER_UNIFORM_VEC4);
            SetShaderValue(s_shader.shader, s_shader.travel, &travel, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_shader.shader, s_shader.lifeFade, &lifeFade, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_shader.shader, s_shader.coreEmission, &stroke->config.coreEmission,
                           SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_shader.shader, s_shader.haloEmission, &stroke->config.haloEmission,
                           SHADER_UNIFORM_FLOAT);
        }
        LightningStroke_DrawWarpedSheet(stroke, camera);
    }
    if (shaded) SkillManager_EndShader();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
}
