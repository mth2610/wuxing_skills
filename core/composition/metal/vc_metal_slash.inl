// vc_metal_slash.inl — Refined Dual-Pass Metal Sword-Qi Compositions
// Referenced from vc_sweep_slash.inl (192x128 asymmetric dual-sheet mask,
// outer-edge-aligned passes, arc-length-keyed 1:20 aspect ratio, tip sparks,
// and screen distortion) adapted for Bright-Background Dark-Steel + Platinum contrast.

#ifndef VC_METAL_SLASH_INL
#define VC_METAL_SLASH_INL

#ifndef PI
#define PI 3.1415926535f
#endif

#define VC_METAL_SLASH_MAX     16
#define VC_METAL_LANCE_MAX     16
#define VC_METAL_CRESCENT_MAX  16
#define VC_METAL_SEVER_MAX     8
#define VC_METAL_SLASH_PTS     32

typedef struct {
    bool    active;
    Vector3 origin;
    Vector3 dir;
    Vector3 planeNormal;
    float   radius;
    float   arcRad;      /* signed: positive = CCW, negative = CW (reverse swing) */
    float   elapsed;
    float   duration;
    float   scale;
    bool    groundScar;
    bool    scarSpawned;
    float   sparkAcc;
    float   distortAcc;
} VC_MetalSlashInst;

typedef struct {
    bool    active;
    int     id;
    Vector3 pos;
    Vector3 dir;
    float   elapsed;
    float   duration;
    float   scale;
    float   rollDeg;
    Matrix  trailXf;
    int     trailHandle;
} VC_MetalLanceInst;

typedef struct {
    bool    active;
    Vector3 origin;
    Vector3 dir;
    float   rollDeg;
    float   range;
    float   coneRad;
    float   elapsed;
    float   duration;
    float   scale;
    float   dist;
    float   sparkAccum;
} VC_MetalCrescentInst;

typedef struct {
    bool    active;
    Vector3 startPos;
    Vector3 endPos;
    float   elapsed;
    float   duration;
    float   scale;
    bool    snapped;
} VC_MetalSeverInst;

static VC_MetalSlashInst    s_metalSlashes[VC_METAL_SLASH_MAX];
static VC_MetalLanceInst    s_metalLances[VC_METAL_LANCE_MAX];
static VC_MetalCrescentInst s_metalCrescents[VC_METAL_CRESCENT_MAX];
static VC_MetalSeverInst    s_metalSevers[VC_METAL_SEVER_MAX];
static int                  s_metalLanceNextId = 1;

static Texture2D s_metalSlashTex = {0};
static Texture2D s_metalHaloTex  = {0};

static float MetalSlash_Profile(float u, bool withRim)
{
    float e = 1.0f - u; /* u = 0 is OUTER razor edge, u = 1 is inner smear */
    float aa = 1.0f - SmoothStep01((e - 0.985f) / 0.015f);
    if (!withRim) {
        return powf(e, 1.7f) * aa;
    }
    float body = powf(e, 2.4f);
    float d    = u / 0.05f;
    float rim  = expf(-d * d);
    return (0.40f * body + 0.72f * rim) * aa;
}

static void MetalSlash_EnsureMasks(void)
{
    if (s_metalSlashTex.id != 0) return;

    const int W = 192;
    const int H = 128;
    Image img  = GenImageColor(W, H, BLANK);
    Image halo = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++) {
        float v = ((float)y + 0.5f) / (float)H;
        for (int x = 0; x < W; x++) {
            float u = ((float)x + 0.5f) / (float)W;
            float lines  = 0.72f + 0.28f * sinf(u * PI * 9.0f  + v * 1.7f);
            float lines2 = 0.85f + 0.15f * sinf(u * PI * 23.0f - v * 0.6f);
            float pulse  = 0.80f + 0.20f * sinf(v * PI * 5.0f  + u * 2.3f);

            float a = Clamp(MetalSlash_Profile(u, true) * lines * lines2 * pulse, 0.0f, 1.0f);
            ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(a * 255.0f) });

            float h = Clamp(MetalSlash_Profile(u, false) * pulse, 0.0f, 1.0f);
            ImageDrawPixel(&halo, x, y, (Color){ 255, 255, 255, (unsigned char)(h * 255.0f) });
        }
    }
    s_metalSlashTex = LoadTextureFromImage(img);
    s_metalHaloTex  = LoadTextureFromImage(halo);
    UnloadImage(img);
    UnloadImage(halo);
    if (s_metalSlashTex.id != 0) {
        SetTextureFilter(s_metalSlashTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_metalSlashTex, TEXTURE_WRAP_CLAMP);
    }
    if (s_metalHaloTex.id != 0) {
        SetTextureFilter(s_metalHaloTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_metalHaloTex, TEXTURE_WRAP_CLAMP);
    }
}

static Texture2D MetalSlash_GetMask(void)
{
    MetalSlash_EnsureMasks();
    return s_metalSlashTex;
}

static void MetalSlash_DrawDualRibbon(const RibbonPoint *bodyPts, const RibbonPoint *edgePts,
                                      const RibbonPoint *haloPts, int count, Camera3D cam,
                                      RibbonMode mode, Vector3 normal)
{
    MetalSlash_EnsureMasks();
    /* Pass 1: Dark tempered gunmetal body (high darken% on bright backgrounds) */
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        DrawRibbonStripEx(bodyPts, count, s_metalSlashTex, cam, mode, normal);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
    /* Pass 2: Soft steel-blue halo (rim-less) + razor platinum cutting edge */
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        if (haloPts && s_metalHaloTex.id != 0) {
            DrawRibbonStripEx(haloPts, count, s_metalHaloTex, cam, mode, normal);
        }
        DrawRibbonStripEx(edgePts, count, s_metalSlashTex, cam, mode, normal);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

int VFX_MetalSlash_Spawn(Vector3 origin, Vector3 dir, Vector3 planeNormal,
                         float radius, float arcDeg, float duration,
                         float scale, bool groundScar)
{
    int slot = -1;
    for (int i = 0; i < VC_METAL_SLASH_MAX; i++) {
        if (!s_metalSlashes[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    Vector3 d = (Vector3LengthSqr(dir) > 1e-5f) ? Vector3Normalize(dir) : (Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 n = (Vector3LengthSqr(planeNormal) > 1e-5f) ? Vector3Normalize(planeNormal) : (Vector3){ 0.0f, 1.0f, 0.0f };
    n = Vector3Subtract(n, Vector3Scale(d, Vector3DotProduct(n, d)));
    if (Vector3LengthSqr(n) < 1e-5f) n = (Vector3){ 0.0f, 1.0f, 0.0f };
    else n = Vector3Normalize(n);

    float sgn = (arcDeg < 0.0f) ? -1.0f : 1.0f;
    float mag = Clamp(fabsf(arcDeg), 30.0f, 240.0f);

    VC_MetalSlashInst *inst = &s_metalSlashes[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active      = true;
    inst->origin      = origin;
    inst->dir         = d;
    inst->planeNormal = n;
    inst->radius      = (radius > 0.2f) ? radius : 2.2f;
    inst->arcRad      = sgn * mag * DEG2RAD;
    inst->duration    = (duration > 0.05f) ? duration : 0.30f;
    inst->scale       = (scale > 0.1f) ? scale : 1.0f;
    inst->groundScar  = groundScar;
    return slot + 1;
}

int VFX_MetalLance_Spawn(Vector3 pos, Vector3 dir, float scale, float duration)
{
    MetalBlade_InitMesh();
    int slot = -1;
    for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
        if (!s_metalLances[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_metalLances[0].trailHandle >= 0) VFX_KillTrail(s_metalLances[0].trailHandle);
    }

    Vector3 d = (Vector3LengthSqr(dir) > 1e-5f) ? Vector3Normalize(dir) : (Vector3){ 0.0f, 0.0f, 1.0f };
    VC_MetalLanceInst *inst = &s_metalLances[slot];
    inst->active      = true;
    inst->id          = s_metalLanceNextId++;
    inst->pos         = pos;
    inst->dir         = d;
    inst->elapsed     = 0.0f;
    inst->duration    = (duration > 0.05f) ? duration : 0.65f;
    inst->scale       = (scale > 0.1f) ? scale : 1.0f;
    inst->rollDeg     = 0.0f;
    inst->trailXf     = MetalBlade_BuildTransform(pos, d, 0.0f, 1.0f);
    inst->trailHandle = VFX_ComposeTrail(&inst->trailXf, VC_MAT_METAL, 0.065f * inst->scale, 0.20f, TRAIL_PRESET_BLADE);
    return inst->id;
}

void VFX_MetalLance_SetPos(int handle, Vector3 pos, Vector3 dir)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
        if (s_metalLances[i].active && s_metalLances[i].id == handle) {
            s_metalLances[i].pos = pos;
            if (Vector3LengthSqr(dir) > 1e-5f) s_metalLances[i].dir = Vector3Normalize(dir);
            s_metalLances[i].trailXf = MetalBlade_BuildTransform(pos, s_metalLances[i].dir, s_metalLances[i].rollDeg, 1.0f);
            return;
        }
    }
}

void VFX_MetalLance_Kill(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
        if (s_metalLances[i].active && s_metalLances[i].id == handle) {
            if (s_metalLances[i].trailHandle >= 0) {
                VFX_TrailSetWidth(s_metalLances[i].trailHandle, 0.0f);
                VFX_KillTrail(s_metalLances[i].trailHandle);
                s_metalLances[i].trailHandle = -1;
            }
            s_metalLances[i].active = false;
            return;
        }
    }
}

int VFX_MetalCrescentWave_Spawn(Vector3 origin, Vector3 dir, float rollDeg,
                                float range, float coneDeg, float duration, float scale)
{
    int slot = -1;
    for (int i = 0; i < VC_METAL_CRESCENT_MAX; i++) {
        if (!s_metalCrescents[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    Vector3 d = { dir.x, 0.0f, dir.z };
    d = (Vector3LengthSqr(d) > 1e-5f) ? Vector3Normalize(d) : (Vector3){ 0.0f, 0.0f, 1.0f };

    VC_MetalCrescentInst *inst = &s_metalCrescents[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active     = true;
    inst->origin     = origin;
    inst->dir        = d;
    inst->rollDeg    = rollDeg;
    inst->range      = (range > 0.5f) ? range : 5.5f;
    inst->coneRad    = Clamp(coneDeg, 20.0f, 140.0f) * DEG2RAD;
    inst->duration   = (duration > 0.05f) ? duration : 0.36f;
    inst->scale      = (scale > 0.1f) ? scale : 1.0f;
    inst->dist       = 0.85f;
    inst->sparkAccum = 0.03f;
    return slot + 1;
}

int VFX_MetalSeverLine_Spawn(Vector3 startPos, Vector3 endPos, float duration, float scale)
{
    int slot = -1;
    for (int i = 0; i < VC_METAL_SEVER_MAX; i++) {
        if (!s_metalSevers[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_MetalSeverInst *inst = &s_metalSevers[slot];
    inst->active   = true;
    inst->startPos = startPos;
    inst->endPos   = endPos;
    inst->elapsed  = 0.0f;
    inst->duration = (duration > 0.05f) ? duration : 0.36f;
    inst->scale    = (scale > 0.1f) ? scale : 1.0f;
    inst->snapped  = false;
    return slot + 1;
}

void VC_MetalSlash_Update(float dt)
{
    for (int i = 0; i < VC_METAL_SLASH_MAX; i++) {
        VC_MetalSlashInst *inst = &s_metalSlashes[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);

        if (inst->groundScar && !inst->scarSpawned && t01 >= 0.48f) {
            inst->scarSpawned = true;
            Vector3 impactPt = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->radius * 0.85f));
            impactPt.y = VFX_GroundHeightFromMap(impactPt.x, impactPt.z, NULL) + 0.04f;
            VFX_ComposeContactSparkMode(impactPt, VC_MAT_METAL, 0.75f * inst->scale, 0.85f, CONTACT_SPARK_CENTRIFUGAL);
            VFX_ComposeFissureStreak(inst->origin, Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->radius)),
                                     0.14f * inst->scale, 0.35f, inst->elapsed);
        }
        if (inst->elapsed >= inst->duration) inst->active = false;
    }

    for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
        VC_MetalLanceInst *inst = &s_metalLances[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        inst->rollDeg += 1080.0f * dt;
        inst->trailXf = MetalBlade_BuildTransform(inst->pos, inst->dir, inst->rollDeg, 1.0f);
        if (inst->elapsed >= inst->duration) {
            if (inst->trailHandle >= 0) {
                VFX_TrailSetWidth(inst->trailHandle, 0.0f);
                VFX_KillTrail(inst->trailHandle);
                inst->trailHandle = -1;
            }
            inst->active = false;
        }
    }

    for (int i = 0; i < VC_METAL_CRESCENT_MAX; i++) {
        VC_MetalCrescentInst *inst = &s_metalCrescents[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float ease = 1.0f - powf(1.0f - t01, 2.2f);
        inst->dist = 0.85f + (inst->range - 0.85f) * ease;

        inst->sparkAccum += dt;
        if (inst->sparkAccum >= 0.055f && t01 < 0.82f) {
            inst->sparkAccum = 0.0f;
            float sideOff = (Random01() - 0.5f) * inst->dist * 1.1f;
            Vector3 right = { -inst->dir.z, 0.0f, inst->dir.x };
            Vector3 sp = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->dist * 0.94f));
            sp = Vector3Add(sp, Vector3Scale(right, sideOff));
            sp.y = VFX_GroundHeightFromMap(sp.x, sp.z, NULL) + 0.14f;
            VFX_ComposeContactSparkMode(sp, VC_MAT_METAL, 0.36f * inst->scale, 0.60f, CONTACT_SPARK_CENTRIFUGAL);
        }
        if (inst->elapsed >= inst->duration) inst->active = false;
    }

    for (int i = 0; i < VC_METAL_SEVER_MAX; i++) {
        VC_MetalSeverInst *inst = &s_metalSevers[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        float t01 = inst->elapsed / inst->duration;
        if (!inst->snapped && t01 >= 0.72f) {
            inst->snapped = true;
            Vector3 mid = Vector3Lerp(inst->startPos, inst->endPos, 0.55f);
            VFX_ComposeContactSparkMode(mid, VC_MAT_METAL, 0.90f * inst->scale, 0.95f, CONTACT_SPARK_CENTRIFUGAL);
            VFX_ComposeShockRing(mid, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_METAL, 1.5f * inst->scale, 0.15f);
        }
        if (inst->elapsed >= inst->duration) inst->active = false;
    }
}

static void MetalSlash_DrawOneSlash(const VC_MetalSlashInst *inst, Camera3D cam)
{
    (void)cam;
    float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
    VFX_ComposeSweepSlashEx(inst->origin, inst->dir, inst->planeNormal,
                            VC_MAT_METAL, inst->radius * inst->scale, inst->arcRad, t01);
}

static void MetalSlash_DrawOneCrescentWave(const VC_MetalCrescentInst *inst, Camera3D cam)
{
    (void)cam;
    float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
    Vector3 up    = { 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, inst->dir));
    float roll    = inst->rollDeg * DEG2RAD;
    Vector3 nPlane = Vector3Normalize(Vector3Add(Vector3Scale(up, cosf(roll)), Vector3Scale(right, sinf(roll))));

    /* Advance the sweep origin forward along the cone as the wave propagates */
    float adv = fmaxf(0.0f, inst->dist - 1.4f);
    Vector3 advOrigin = Vector3Add(inst->origin, Vector3Scale(inst->dir, adv));
    advOrigin.y = VFX_GroundHeightFromMap(advOrigin.x, advOrigin.z, NULL) + 0.92f;
    float signedArc = (inst->rollDeg < 0.0f) ? -inst->coneRad : inst->coneRad;
    VFX_ComposeSweepSlashEx(advOrigin, inst->dir, nPlane,
                            VC_MAT_METAL, 1.85f * inst->scale, signedArc * 1.45f, t01);
}

static void MetalSlash_DrawOneSeverLine(const VC_MetalSeverInst *inst, Camera3D cam)
{
    float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
    float peakEnv = (t01 < 0.72f) ? 1.0f : powf(1.0f - (t01 - 0.72f) / 0.28f, 1.6f);
    if (peakEnv <= 0.01f) return;

    static RibbonPoint bodyPts[16];
    static RibbonPoint haloPts[16];
    static RibbonPoint edgePts[16];
    Color steelDark = (Color){ 24, 32, 44, 255 };
    Color steelGlow = (Color){ 145, 205, 255, 255 };
    Color platinum  = (Color){ 245, 250, 255, 255 };

    float widthPulse = (t01 < 0.18f) ? 1.55f : (t01 < 0.72f ? 1.00f : 0.50f * peakEnv);
    for (int i = 0; i < 16; i++) {
        float u = (float)i / 15.0f;
        Vector3 p = Vector3Lerp(inst->startPos, inst->endPos, u);
        float env = powf(sinf(u * PI), 0.50f);

        bodyPts[i].position  = p;
        bodyPts[i].halfWidth = 0.20f * inst->scale * env * widthPulse;
        bodyPts[i].tint      = ColorAlpha(steelDark, Clamp(peakEnv * 0.94f, 0.0f, 1.0f));
        bodyPts[i].v         = u;

        haloPts[i].position  = p;
        haloPts[i].halfWidth = 0.28f * inst->scale * env * widthPulse;
        haloPts[i].tint      = VC_Premultiply(steelGlow, Clamp(peakEnv * 0.26f, 0.0f, 1.0f));
        haloPts[i].v         = u;

        edgePts[i].position  = p;
        edgePts[i].halfWidth = 0.055f * inst->scale * env * widthPulse;
        edgePts[i].tint      = VC_Premultiply(platinum, Clamp(peakEnv, 0.0f, 1.0f));
        edgePts[i].v         = u;
    }
    MetalSlash_DrawDualRibbon(bodyPts, edgePts, haloPts, 16, cam, RIBBON_CAMERA_FACING, (Vector3){ 0.0f, 1.0f, 0.0f });
}

void VFX_MetalIaidoCoil_Draw(Vector3 pos, Vector3 dir, float progress, float range, Camera3D cam)
{
    if (progress < 0.0f || progress >= 1.0f) return;
    Vector3 d = { dir.x, 0.0f, dir.z };
    d = (Vector3LengthSqr(d) > 1e-5f) ? Vector3Normalize(d) : (Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 rgt = { -d.z, 0.0f, d.x };

    float reach = range * (0.25f + 0.75f * Clamp(progress / 0.75f, 0.0f, 1.0f));
    static RibbonPoint bodyPts[12];
    static RibbonPoint edgePts[12];
    Color steelDark = (Color){ 28, 36, 48, 255 };
    Color silverHot = (Color){ 220, 240, 255, 255 };
    float alpha = 0.40f + 0.55f * progress;

    for (int i = 0; i < 12; i++) {
        float u = (float)i / 11.0f;
        Vector3 wp = Vector3Add(pos, Vector3Scale(d, reach * u));
        wp.y = VFX_GroundHeightFromMap(wp.x, wp.z, NULL) + 0.06f;
        float env = (1.0f - u * 0.65f) * ((u < 0.1f) ? (u / 0.1f) : 1.0f);

        bodyPts[i].position  = wp;
        bodyPts[i].halfWidth = 0.075f * env;
        bodyPts[i].tint      = ColorAlpha(steelDark, Clamp(alpha * 0.80f, 0.0f, 1.0f));
        bodyPts[i].v         = u;

        edgePts[i].position  = wp;
        edgePts[i].halfWidth = 0.022f * env;
        edgePts[i].tint      = VC_Premultiply(silverHot, Clamp(alpha * 0.90f, 0.0f, 1.0f));
        edgePts[i].v         = u;
    }
    MetalSlash_DrawDualRibbon(bodyPts, edgePts, NULL, 12, cam, RIBBON_WORLD_UP, (Vector3){ 0.0f, 1.0f, 0.0f });

    Vector3 hiltPos = Vector3Add(pos, Vector3Add(Vector3Scale(rgt, -0.18f), (Vector3){ 0.0f, 0.78f, 0.0f }));
    float starRad   = 0.14f + 0.12f * progress;
    float streakLen = 0.45f + 1.15f * powf(progress, 2.0f);
    VFX_DrawOpticalStarburstStreak(hiltPos, (Color){ 255, 255, 255, 255 }, (Color){ 205, 232, 255, 255 },
                                   starRad, streakLen, 0.024f, 0.65f + 0.55f * progress, cam);
}

void VC_MetalSlash_Draw3D(Camera3D cam)
{
    for (int i = 0; i < VC_METAL_SLASH_MAX; i++) {
        if (s_metalSlashes[i].active) MetalSlash_DrawOneSlash(&s_metalSlashes[i], cam);
    }
    for (int i = 0; i < VC_METAL_CRESCENT_MAX; i++) {
        if (s_metalCrescents[i].active) MetalSlash_DrawOneCrescentWave(&s_metalCrescents[i], cam);
    }
    for (int i = 0; i < VC_METAL_SEVER_MAX; i++) {
        if (s_metalSevers[i].active) MetalSlash_DrawOneSeverLine(&s_metalSevers[i], cam);
    }

    bool anyLance = false;
    for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
        if (s_metalLances[i].active) { anyLance = true; break; }
    }
    if (anyLance && s_bladeShader.id != 0 && s_subSteel.mesh.vertexCount > 0) {
        s_bladeDrawMat.shader = s_bladeShader;
        float now = TimeFX_Elapsed();
        if (s_bladeLocTime >= 0) SetShaderValue(s_bladeShader, s_bladeLocTime, &now, SHADER_UNIFORM_FLOAT);

        float bladeBase[4] = { 0.52f, 0.58f, 0.68f, 1.0f };
        float bladeRim[4]  = { 0.82f, 0.92f, 1.00f, 1.0f };
        float goldBase[4]  = { 0.85f, 0.65f, 0.22f, 1.0f };
        float goldRim[4]   = { 1.00f, 0.88f, 0.45f, 1.0f };

        for (int i = 0; i < VC_METAL_LANCE_MAX; i++) {
            const VC_MetalLanceInst *inst = &s_metalLances[i];
            if (!inst->active) continue;
            Matrix xf = MetalBlade_BuildTransform(inst->pos, inst->dir, inst->rollDeg, 0.92f * inst->scale);
            float ph = (float)i * 0.7f;
            if (s_bladeLocPhase >= 0) SetShaderValue(s_bladeShader, s_bladeLocPhase, &ph, SHADER_UNIFORM_FLOAT);

            float mode = 0.0f;
            if (s_bladeLocPartMode >= 0)  SetShaderValue(s_bladeShader, s_bladeLocPartMode,  &mode,      SHADER_UNIFORM_FLOAT);
            if (s_bladeLocBaseColor >= 0) SetShaderValue(s_bladeShader, s_bladeLocBaseColor, bladeBase, SHADER_UNIFORM_VEC4);
            if (s_bladeLocRimColor >= 0)  SetShaderValue(s_bladeShader, s_bladeLocRimColor,  bladeRim,  SHADER_UNIFORM_VEC4);
            DrawMesh(s_subSteel.mesh, s_bladeDrawMat, xf);

            mode = 1.0f;
            if (s_bladeLocPartMode >= 0)  SetShaderValue(s_bladeShader, s_bladeLocPartMode,  &mode,     SHADER_UNIFORM_FLOAT);
            if (s_bladeLocBaseColor >= 0) SetShaderValue(s_bladeShader, s_bladeLocBaseColor, goldBase, SHADER_UNIFORM_VEC4);
            if (s_bladeLocRimColor >= 0)  SetShaderValue(s_bladeShader, s_bladeLocRimColor,  goldRim,  SHADER_UNIFORM_VEC4);
            DrawMesh(s_subGold.mesh, s_bladeDrawMat, xf);
        }
    }
}

#endif /* VC_METAL_SLASH_INL */
