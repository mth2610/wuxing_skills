// ── COHESIVE VOLUMETRIC SAND / DUST FUNNEL (3-LAYER ALPHA BODY) ─────────────
// Layer 1: Ground Sweeping Dust Skirt (domed logarithmic-spiral sand annulus)
// Layer 2: Inner Core Volumetric Funnel (3-zone bell-waist-anvil + sinuous spine)
// Layer 3: Outer Wind-Shear Veil (wider fast-spinning wispy shear curtain)
//
// Zero dynamic allocation (static pool + lazy cached cylinder & annulus VAOs).
// Drawn in VFX_RENDER_PASS_BODY / VFX_SURFACE_ALPHA (Bright-Background compliant).
#ifndef VC_SAND_FUNNEL_INL
#define VC_SAND_FUNNEL_INL

#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/resource_manager.h"
#include "core/vfx_render.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define SAND_FUNNEL_MAX_INSTANCES 8
#define SAND_FUNNEL_CYL_RAD_SEGS  24
#define SAND_FUNNEL_CYL_H_SEGS    16
#define SAND_FUNNEL_SKIRT_RAD_SEGS 24
#define SAND_FUNNEL_SKIRT_RINGS    5

typedef struct {
    bool active;
    bool stopping;
    int  id;
    VC_MaterialId matId;
    VFX_SandFunnelConfig cfg;

    Vector3 basePos;
    Vector3 velocity;
    Vector2 tilt;
    float   power;
    float   targetPower;
    float   implosion;
    float   targetImplosion;
    float   elapsed;
    float   spineTime;
} VC_SandFunnelInstance;

static VC_SandFunnelInstance s_sandFunnels[SAND_FUNNEL_MAX_INSTANCES];
static int s_sandFunnelNextId = 1;
static bool s_sandFunnelGpuReady = false;
static bool s_sandFunnelGpuAttempted = false;

static Shader s_sandFunnelShader = {0};
static Shader s_sandSkirtShader = {0};

static unsigned int s_funnelVao = 0, s_funnelVboPos = 0, s_funnelVboUv = 0, s_funnelEbo = 0;
static int s_funnelIndexCount = 0;

static unsigned int s_skirtVao = 0, s_skirtVboPos = 0, s_skirtEbo = 0;
static int s_skirtIndexCount = 0;

static struct {
    int view, projection, time, spineTime, basePos, height, baseRadius, topRadius;
    int swayAmp, tilt, power, implosion, radiusMul, spinSpeed, waveAmp;
    int camPos, lightDir, litColor, midColor, shadowColor, rimColor;
    int uvScaleU, uvScaleV, scrollU, scrollV, shearSlope;
    int opacity, erosionMin, erosionMax, fresnelPow, coreShadowBoost;
} s_sfUni;

static struct {
    int view, projection, time, basePos, skirtRadius, domeLift;
    int power, implosion, lightDir, litColor, midColor, shadowColor;
} s_skUni;

static void SandFunnel_InitGpu(void)
{
    if (s_sandFunnelGpuAttempted) return;
    s_sandFunnelGpuAttempted = true;

    s_sandFunnelShader = ResourceManager_LoadShader("core/shaders/sand_funnel.vs",
                                                    "core/shaders/sand_funnel.fs");
    s_sandSkirtShader  = ResourceManager_LoadShader("core/shaders/sand_skirt.vs",
                                                    "core/shaders/sand_skirt.fs");
    if (!s_sandFunnelShader.id || s_sandFunnelShader.id == rlGetShaderIdDefault() ||
        !s_sandSkirtShader.id  || s_sandSkirtShader.id  == rlGetShaderIdDefault()) {
        return;
    }

    /* Build static open unit cylinder (radius 1, y in [0..1]) */
    enum {
        CYL_VERTS = (SAND_FUNNEL_CYL_RAD_SEGS + 1) * (SAND_FUNNEL_CYL_H_SEGS + 1),
        CYL_INDICES = SAND_FUNNEL_CYL_RAD_SEGS * SAND_FUNNEL_CYL_H_SEGS * 6
    };
    static float s_cylPos[CYL_VERTS * 3];
    static float s_cylUv[CYL_VERTS * 2];
    static unsigned short s_cylIdx[CYL_INDICES];

    int v = 0;
    for (int iy = 0; iy <= SAND_FUNNEL_CYL_H_SEGS; iy++) {
        float h = (float)iy / (float)SAND_FUNNEL_CYL_H_SEGS;
        for (int ix = 0; ix <= SAND_FUNNEL_CYL_RAD_SEGS; ix++) {
            float u = (float)ix / (float)SAND_FUNNEL_CYL_RAD_SEGS;
            float ang = u * 6.2831853f;
            s_cylPos[v * 3 + 0] = cosf(ang);
            s_cylPos[v * 3 + 1] = h;
            s_cylPos[v * 3 + 2] = sinf(ang);
            s_cylUv[v * 2 + 0] = u;
            s_cylUv[v * 2 + 1] = h;
            v++;
        }
    }
    int k = 0;
    int stride = SAND_FUNNEL_CYL_RAD_SEGS + 1;
    for (int iy = 0; iy < SAND_FUNNEL_CYL_H_SEGS; iy++) {
        for (int ix = 0; ix < SAND_FUNNEL_CYL_RAD_SEGS; ix++) {
            unsigned short a = (unsigned short)(iy * stride + ix);
            unsigned short b = (unsigned short)(a + 1);
            unsigned short c = (unsigned short)(a + stride);
            unsigned short d = (unsigned short)(c + 1);
            s_cylIdx[k++] = a; s_cylIdx[k++] = b; s_cylIdx[k++] = c;
            s_cylIdx[k++] = b; s_cylIdx[k++] = d; s_cylIdx[k++] = c;
        }
    }
    s_funnelIndexCount = k;

    s_funnelVao = rlLoadVertexArray();
    rlEnableVertexArray(s_funnelVao);
    s_funnelVboPos = rlLoadVertexBuffer(s_cylPos, sizeof(s_cylPos), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(0);
    s_funnelVboUv = rlLoadVertexBuffer(s_cylUv, sizeof(s_cylUv), false);
    rlSetVertexAttribute(1, 2, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(1);
    s_funnelEbo = rlLoadVertexBufferElement(s_cylIdx, sizeof(s_cylIdx), false);
    rlDisableVertexArray();

    /* Build static horizontal ground annulus (r in [0.16..1.0]) */
    enum {
        SK_VERTS = (SAND_FUNNEL_SKIRT_RAD_SEGS + 1) * (SAND_FUNNEL_SKIRT_RINGS + 1),
        SK_INDICES = SAND_FUNNEL_SKIRT_RAD_SEGS * SAND_FUNNEL_SKIRT_RINGS * 6
    };
    static float s_skPos[SK_VERTS * 3];
    static unsigned short s_skIdx[SK_INDICES];

    v = 0;
    for (int ir = 0; ir <= SAND_FUNNEL_SKIRT_RINGS; ir++) {
        float rt = (float)ir / (float)SAND_FUNNEL_SKIRT_RINGS;
        float r = 0.16f + 0.84f * rt;
        for (int ix = 0; ix <= SAND_FUNNEL_SKIRT_RAD_SEGS; ix++) {
            float ang = ((float)ix / (float)SAND_FUNNEL_SKIRT_RAD_SEGS) * 6.2831853f;
            s_skPos[v * 3 + 0] = cosf(ang) * r;
            s_skPos[v * 3 + 1] = 0.0f;
            s_skPos[v * 3 + 2] = sinf(ang) * r;
            v++;
        }
    }
    k = 0;
    int skStride = SAND_FUNNEL_SKIRT_RAD_SEGS + 1;
    for (int ir = 0; ir < SAND_FUNNEL_SKIRT_RINGS; ir++) {
        for (int ix = 0; ix < SAND_FUNNEL_SKIRT_RAD_SEGS; ix++) {
            unsigned short a = (unsigned short)(ir * skStride + ix);
            unsigned short b = (unsigned short)(a + 1);
            unsigned short c = (unsigned short)(a + skStride);
            unsigned short d = (unsigned short)(c + 1);
            s_skIdx[k++] = a; s_skIdx[k++] = b; s_skIdx[k++] = c;
            s_skIdx[k++] = b; s_skIdx[k++] = d; s_skIdx[k++] = c;
        }
    }
    s_skirtIndexCount = k;

    s_skirtVao = rlLoadVertexArray();
    rlEnableVertexArray(s_skirtVao);
    s_skirtVboPos = rlLoadVertexBuffer(s_skPos, sizeof(s_skPos), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, 0, 0, 0);
    rlEnableVertexAttribute(0);
    s_skirtEbo = rlLoadVertexBufferElement(s_skIdx, sizeof(s_skIdx), false);
    rlDisableVertexArray();

    /* Cache uniform locations once */
    s_sfUni.view            = GetShaderLocation(s_sandFunnelShader, "u_view");
    s_sfUni.projection      = GetShaderLocation(s_sandFunnelShader, "u_projection");
    s_sfUni.time            = GetShaderLocation(s_sandFunnelShader, "u_time");
    s_sfUni.spineTime       = GetShaderLocation(s_sandFunnelShader, "u_spineTime");
    s_sfUni.basePos         = GetShaderLocation(s_sandFunnelShader, "u_basePos");
    s_sfUni.height          = GetShaderLocation(s_sandFunnelShader, "u_height");
    s_sfUni.baseRadius      = GetShaderLocation(s_sandFunnelShader, "u_baseRadius");
    s_sfUni.topRadius       = GetShaderLocation(s_sandFunnelShader, "u_topRadius");
    s_sfUni.swayAmp         = GetShaderLocation(s_sandFunnelShader, "u_swayAmp");
    s_sfUni.tilt            = GetShaderLocation(s_sandFunnelShader, "u_tilt");
    s_sfUni.power           = GetShaderLocation(s_sandFunnelShader, "u_power");
    s_sfUni.implosion       = GetShaderLocation(s_sandFunnelShader, "u_implosion");
    s_sfUni.radiusMul       = GetShaderLocation(s_sandFunnelShader, "u_radiusMul");
    s_sfUni.spinSpeed       = GetShaderLocation(s_sandFunnelShader, "u_spinSpeed");
    s_sfUni.waveAmp         = GetShaderLocation(s_sandFunnelShader, "u_waveAmp");
    s_sfUni.camPos          = GetShaderLocation(s_sandFunnelShader, "u_camPos");
    s_sfUni.lightDir        = GetShaderLocation(s_sandFunnelShader, "u_lightDir");
    s_sfUni.litColor        = GetShaderLocation(s_sandFunnelShader, "u_litColor");
    s_sfUni.midColor        = GetShaderLocation(s_sandFunnelShader, "u_midColor");
    s_sfUni.shadowColor     = GetShaderLocation(s_sandFunnelShader, "u_shadowColor");
    s_sfUni.rimColor        = GetShaderLocation(s_sandFunnelShader, "u_rimColor");
    s_sfUni.uvScaleU        = GetShaderLocation(s_sandFunnelShader, "u_uvScaleU");
    s_sfUni.uvScaleV        = GetShaderLocation(s_sandFunnelShader, "u_uvScaleV");
    s_sfUni.scrollU         = GetShaderLocation(s_sandFunnelShader, "u_scrollU");
    s_sfUni.scrollV         = GetShaderLocation(s_sandFunnelShader, "u_scrollV");
    s_sfUni.shearSlope      = GetShaderLocation(s_sandFunnelShader, "u_shearSlope");
    s_sfUni.opacity         = GetShaderLocation(s_sandFunnelShader, "u_opacity");
    s_sfUni.erosionMin      = GetShaderLocation(s_sandFunnelShader, "u_erosionMin");
    s_sfUni.erosionMax      = GetShaderLocation(s_sandFunnelShader, "u_erosionMax");
    s_sfUni.fresnelPow      = GetShaderLocation(s_sandFunnelShader, "u_fresnelPow");
    s_sfUni.coreShadowBoost = GetShaderLocation(s_sandFunnelShader, "u_coreShadowBoost");

    s_skUni.view        = GetShaderLocation(s_sandSkirtShader, "u_view");
    s_skUni.projection  = GetShaderLocation(s_sandSkirtShader, "u_projection");
    s_skUni.time        = GetShaderLocation(s_sandSkirtShader, "u_time");
    s_skUni.basePos     = GetShaderLocation(s_sandSkirtShader, "u_basePos");
    s_skUni.skirtRadius = GetShaderLocation(s_sandSkirtShader, "u_skirtRadius");
    s_skUni.domeLift    = GetShaderLocation(s_sandSkirtShader, "u_domeLift");
    s_skUni.power       = GetShaderLocation(s_sandSkirtShader, "u_power");
    s_skUni.implosion   = GetShaderLocation(s_sandSkirtShader, "u_implosion");
    s_skUni.lightDir    = GetShaderLocation(s_sandSkirtShader, "u_lightDir");
    s_skUni.litColor    = GetShaderLocation(s_sandSkirtShader, "u_litColor");
    s_skUni.midColor    = GetShaderLocation(s_sandSkirtShader, "u_midColor");
    s_skUni.shadowColor = GetShaderLocation(s_sandSkirtShader, "u_shadowColor");

    s_sandFunnelGpuReady = (s_funnelVao != 0 && s_skirtVao != 0);
}

VFX_SandFunnelConfig VFX_SandFunnel_DefaultConfig(void)
{
    return (VFX_SandFunnelConfig){
        .baseRadiusM  = 0.34f,
        .topRadiusM   = 1.42f,
        .heightM      = 3.85f,
        .skirtRadiusM = 2.15f,
        .swayAmpM     = 0.28f,
        .spinSpeed    = 4.2f,
        .opacity      = 0.72f,
        .durationSec  = 0.0f /* 0 = caller-controlled until Stop/Kill */
    };
}

void VFX_SandFunnel_Warmup(void)
{
    SandFunnel_InitGpu();
}

static VC_SandFunnelInstance *SandFunnel_Find(int handle)
{
    if (handle <= 0) return NULL;
    for (int i = 0; i < SAND_FUNNEL_MAX_INSTANCES; i++) {
        if (s_sandFunnels[i].active && s_sandFunnels[i].id == handle) {
            return &s_sandFunnels[i];
        }
    }
    return NULL;
}

int VFX_ComposeSandFunnel(Vector3 basePos, VC_MaterialId matId, const VFX_SandFunnelConfig *cfg)
{
    VFX_SandFunnelConfig c = cfg ? *cfg : VFX_SandFunnel_DefaultConfig();
    if (c.baseRadiusM <= 0.02f) c.baseRadiusM = 0.34f;
    if (c.topRadiusM  <= 0.05f) c.topRadiusM  = 1.42f;
    if (c.heightM     <= 0.20f) c.heightM     = 3.85f;
    if (c.skirtRadiusM <= 0.1f) c.skirtRadiusM = c.baseRadiusM * 5.6f;
    if (c.spinSpeed   <= 0.05f) c.spinSpeed   = 4.2f;
    if (c.opacity     <= 0.01f) c.opacity     = 0.72f;

    int slot = -1;
    for (int i = 0; i < SAND_FUNNEL_MAX_INSTANCES; i++) {
        if (!s_sandFunnels[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    int id = s_sandFunnelNextId++;
    if (s_sandFunnelNextId <= 0) s_sandFunnelNextId = 1;

    s_sandFunnels[slot] = (VC_SandFunnelInstance){
        .active = true,
        .stopping = false,
        .id = id,
        .matId = matId,
        .cfg = c,
        .basePos = basePos,
        .velocity = {0},
        .tilt = {0},
        .power = 0.05f,
        .targetPower = 1.0f,
        .implosion = 0.0f,
        .targetImplosion = 0.0f,
        .elapsed = 0.0f,
        .spineTime = Random01() * 6.2831853f
    };
    return id;
}

void VFX_SandFunnel_UpdateState(int handle, Vector3 basePos, Vector3 velocity,
                                float power01, float implosion01)
{
    VC_SandFunnelInstance *f = SandFunnel_Find(handle);
    if (!f) return;
    f->basePos = basePos;
    f->velocity = velocity;
    f->targetPower = Clamp(power01, 0.0f, 1.25f);
    f->targetImplosion = Clamp(implosion01, 0.0f, 1.0f);
    if (power01 > 0.05f) f->stopping = false;
}

void VFX_SandFunnel_Stop(int handle)
{
    VC_SandFunnelInstance *f = SandFunnel_Find(handle);
    if (!f) return;
    f->stopping = true;
    f->targetPower = 0.0f;
    f->targetImplosion = 0.0f;
}

void VFX_KillSandFunnel(int handle)
{
    VC_SandFunnelInstance *f = SandFunnel_Find(handle);
    if (f) f->active = false;
}

static void VC_SandFunnel_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < SAND_FUNNEL_MAX_INSTANCES; i++) {
        VC_SandFunnelInstance *f = &s_sandFunnels[i];
        if (!f->active) continue;

        f->elapsed += dt;
        f->spineTime += dt * (1.15f + 0.35f * f->power);
        if (f->cfg.durationSec > 0.0f && f->elapsed >= f->cfg.durationSec) {
            f->stopping = true;
            f->targetPower = 0.0f;
        }

        float pRate = (f->targetPower > f->power) ? 5.2f : 3.2f;
        f->power += (f->targetPower - f->power) * fminf(dt * pRate, 1.0f);
        f->implosion += (f->targetImplosion - f->implosion) * fminf(dt * 6.0f, 1.0f);

        float tgtTiltX = Clamp(f->velocity.x * 0.095f, -0.75f, 0.75f);
        float tgtTiltZ = Clamp(f->velocity.z * 0.095f, -0.75f, 0.75f);
        f->tilt.x += (tgtTiltX - f->tilt.x) * fminf(dt * 4.8f, 1.0f);
        f->tilt.y += (tgtTiltZ - f->tilt.y) * fminf(dt * 4.8f, 1.0f);

        if (f->stopping && f->power < 0.012f) {
            f->active = false;
        }
    }
}

static void SandFunnel_ResolvePalette(VC_MaterialId matId, float lit[3], float mid[3],
                                      float shad[3], float rim[3])
{
    if (matId == VC_MAT_EARTH) {
        /* Golden-hour Dune / Mojave sandstone ochre & deep umber earth palette */
        lit[0]  = 0.902f; lit[1]  = 0.757f; lit[2]  = 0.573f;
        mid[0]  = 0.722f; mid[1]  = 0.545f; mid[2]  = 0.365f;
        shad[0] = 0.322f; shad[1] = 0.224f; shad[2] = 0.149f;
        rim[0]  = 1.000f; rim[1]  = 0.875f; rim[2]  = 0.667f;
        return;
    }
    const VFX_ElementMaterial *m = VFX_Material(matId);
    Color body = m ? m->body : (Color){184, 139, 93, 255};
    Color glow = m ? m->glow : (Color){255, 223, 170, 255};
    mid[0]  = body.r / 255.0f; mid[1]  = body.g / 255.0f; mid[2]  = body.b / 255.0f;
    shad[0] = mid[0] * 0.42f;  shad[1] = mid[1] * 0.40f;  shad[2] = mid[2] * 0.38f;
    lit[0]  = fminf(mid[0] * 1.25f + 0.10f, 1.0f);
    lit[1]  = fminf(mid[1] * 1.22f + 0.08f, 1.0f);
    lit[2]  = fminf(mid[2] * 1.18f + 0.06f, 1.0f);
    rim[0]  = glow.r / 255.0f; rim[1]  = glow.g / 255.0f; rim[2]  = glow.b / 255.0f;
}

static void VC_SandFunnel_Draw3D(Camera3D cam)
{
    bool anyActive = false;
    for (int i = 0; i < SAND_FUNNEL_MAX_INSTANCES; i++) {
        if (s_sandFunnels[i].active && s_sandFunnels[i].power > 0.008f) {
            anyActive = true;
            break;
        }
    }
    if (!anyActive) return;

    SandFunnel_InitGpu();
    if (!s_sandFunnelGpuReady) return;

    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    Matrix projection;
    if (cam.projection == CAMERA_ORTHOGRAPHIC) {
        double top = cam.fovy * 0.5;
        projection = MatrixOrtho(-top * aspect, top * aspect, -top, top, 0.0001, 150.0);
    } else {
        double top = tan(cam.fovy * 0.5 * DEG2RAD);
        projection = MatrixFrustum(-top * aspect, top * aspect, -top, top, 1.0, 1000.0);
    }

    Vector3 sunDir = Environment_GetSunDirection();
    if (Vector3LengthSqr(sunDir) < 1e-4f) sunDir = (Vector3){0.62f, 0.48f, -0.62f};
    else sunDir = Vector3Normalize(Vector3Negate(sunDir)); /* surface -> sun */

    VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    if (!scope.active) return;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    for (int i = 0; i < SAND_FUNNEL_MAX_INSTANCES; i++) {
        const VC_SandFunnelInstance *f = &s_sandFunnels[i];
        if (!f->active || f->power <= 0.008f) continue;

        float litCol[3], midCol[3], shadCol[3], rimCol[3];
        SandFunnel_ResolvePalette(f->matId, litCol, midCol, shadCol, rimCol);

        /* 1. Ground Sweeping Dust Skirt */
        BeginShaderMode(s_sandSkirtShader);
        if (s_skUni.view        >= 0) SetShaderValueMatrix(s_sandSkirtShader, s_skUni.view, view);
        if (s_skUni.projection  >= 0) SetShaderValueMatrix(s_sandSkirtShader, s_skUni.projection, projection);
        if (s_skUni.time        >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.time, &f->elapsed, SHADER_UNIFORM_FLOAT);
        if (s_skUni.basePos     >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.basePos, &f->basePos.x, SHADER_UNIFORM_VEC3);
        float skirtR = f->cfg.skirtRadiusM * (1.0f + f->implosion * 0.35f);
        float domeLift = fmaxf(f->cfg.baseRadiusM * 0.95f, 0.28f);
        if (s_skUni.skirtRadius >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.skirtRadius, &skirtR, SHADER_UNIFORM_FLOAT);
        if (s_skUni.domeLift    >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.domeLift, &domeLift, SHADER_UNIFORM_FLOAT);
        if (s_skUni.power       >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.power, &f->power, SHADER_UNIFORM_FLOAT);
        if (s_skUni.implosion   >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.implosion, &f->implosion, SHADER_UNIFORM_FLOAT);
        if (s_skUni.lightDir    >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.lightDir, &sunDir.x, SHADER_UNIFORM_VEC3);
        if (s_skUni.litColor    >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.litColor, litCol, SHADER_UNIFORM_VEC3);
        if (s_skUni.midColor    >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.midColor, midCol, SHADER_UNIFORM_VEC3);
        if (s_skUni.shadowColor >= 0) SetShaderValue(s_sandSkirtShader, s_skUni.shadowColor, shadCol, SHADER_UNIFORM_VEC3);

        rlEnableVertexArray(s_skirtVao);
        rlDrawVertexArrayElements(0, s_skirtIndexCount, NULL);
        rlDisableVertexArray();
        EndShaderMode();

        /* 2. Inner Core Funnel + 3. Outer Wind-Shear Veil */
        for (int layer = 0; layer < 2; layer++) {
            float radMul   = (layer == 0) ? 0.92f : 1.22f;
            float spinSpd  = f->cfg.spinSpeed * ((layer == 0) ? 0.95f : 1.25f);
            float waveAmp  = (layer == 0) ? 0.055f : 0.095f;
            float uvScaleU = (layer == 0) ? 2.0f  : 3.0f;
            float uvScaleV = (layer == 0) ? 2.2f  : 2.6f;
            float scrollU  = (layer == 0) ? 0.82f : 1.18f;
            float scrollV  = (layer == 0) ? 0.52f : 0.72f;
            float shearSl  = (layer == 0) ? 3.40f : 4.20f;
            float opacity  = f->cfg.opacity * ((layer == 0) ? 1.0f : 0.68f);
            float eroMin   = (layer == 0) ? 0.18f : 0.30f;
            float eroMax   = (layer == 0) ? 0.74f : 0.80f;
            float fresPow  = (layer == 0) ? 1.30f : 1.65f;
            float coreDark = (layer == 0) ? 1.00f : 0.35f;

            BeginShaderMode(s_sandFunnelShader);
            if (s_sfUni.view            >= 0) SetShaderValueMatrix(s_sandFunnelShader, s_sfUni.view, view);
            if (s_sfUni.projection      >= 0) SetShaderValueMatrix(s_sandFunnelShader, s_sfUni.projection, projection);
            if (s_sfUni.time            >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.time, &f->elapsed, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.spineTime       >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.spineTime, &f->spineTime, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.basePos         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.basePos, &f->basePos.x, SHADER_UNIFORM_VEC3);
            if (s_sfUni.height          >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.height, &f->cfg.heightM, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.baseRadius      >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.baseRadius, &f->cfg.baseRadiusM, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.topRadius       >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.topRadius, &f->cfg.topRadiusM, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.swayAmp         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.swayAmp, &f->cfg.swayAmpM, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.tilt            >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.tilt, &f->tilt.x, SHADER_UNIFORM_VEC2);
            if (s_sfUni.power           >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.power, &f->power, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.implosion       >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.implosion, &f->implosion, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.radiusMul       >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.radiusMul, &radMul, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.spinSpeed       >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.spinSpeed, &spinSpd, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.waveAmp         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.waveAmp, &waveAmp, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.camPos          >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.camPos, &cam.position.x, SHADER_UNIFORM_VEC3);
            if (s_sfUni.lightDir        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.lightDir, &sunDir.x, SHADER_UNIFORM_VEC3);
            if (s_sfUni.litColor        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.litColor, litCol, SHADER_UNIFORM_VEC3);
            if (s_sfUni.midColor        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.midColor, midCol, SHADER_UNIFORM_VEC3);
            if (s_sfUni.shadowColor     >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.shadowColor, shadCol, SHADER_UNIFORM_VEC3);
            if (s_sfUni.rimColor        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.rimColor, rimCol, SHADER_UNIFORM_VEC3);
            if (s_sfUni.uvScaleU        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.uvScaleU, &uvScaleU, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.uvScaleV        >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.uvScaleV, &uvScaleV, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.scrollU         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.scrollU, &scrollU, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.scrollV         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.scrollV, &scrollV, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.shearSlope      >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.shearSlope, &shearSl, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.opacity         >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.opacity, &opacity, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.erosionMin      >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.erosionMin, &eroMin, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.erosionMax      >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.erosionMax, &eroMax, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.fresnelPow      >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.fresnelPow, &fresPow, SHADER_UNIFORM_FLOAT);
            if (s_sfUni.coreShadowBoost >= 0) SetShaderValue(s_sandFunnelShader, s_sfUni.coreShadowBoost, &coreDark, SHADER_UNIFORM_FLOAT);

            rlEnableVertexArray(s_funnelVao);
            rlDrawVertexArrayElements(0, s_funnelIndexCount, NULL);
            rlDisableVertexArray();
            EndShaderMode();
        }
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    VFXRender_EndDraw(&scope);
}

#endif // VC_SAND_FUNNEL_INL
