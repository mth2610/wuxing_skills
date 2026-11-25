// ── Fire Martial Primitives (Liệt Hỏa Phần Thiên Cước) ──────────────────────
// 1. VFX_FireKickArc_Spawn       : Dual-pass soot BODY + blackbody EMISSION swept kick arc
// 2. VFX_FireCrescentWave_Spawn  : Continuous 110° advancing blazing crescent kick wave
// 3. VFX_FireCyclone_Spawn       : Hollow-center 3D helical fire-vortex funnel (C11)
// 4. VFX_FirePhoenixWard_Spawn   : 3D orbiting sculpted crimson-gold Phoenix Feathers (Hỏa Vũ)
// 5. VFX_FireScorchFormation_Spawn: Terrain-draped SDF scorching formation + rim braziers (C11)

#define VC_FIRE_KICK_MAX       12
#define VC_FIRE_CRESCENT_MAX   8
#define VC_FIRE_CYCLONE_MAX    6
#define VC_FIRE_PHOENIX_MAX    6
#define VC_FIRE_FORMATION_MAX  6

typedef struct {
    bool active;
    Vector3 origin;
    Vector3 dir;
    Vector3 planeNormal;
    float radius;
    float arcRad;
    float elapsed;
    float duration;
    float scale;
} VC_FireKickArcInst;

typedef struct {
    bool active;
    Vector3 origin;
    Vector3 dir;
    float range;
    float coneRad;
    float elapsed;
    float duration;
    float scale;
} VC_FireCrescentInst;

typedef struct {
    bool active;
    int id;
    Vector3 center;
    float radius;
    float height;
    float elapsed;
    float duration;
} VC_FireCycloneInst;

typedef struct {
    bool active;
    int id;
    Vector3 center;
    float radius;
    int feathersLeft;
    float elapsed;
    float duration;
    bool stopping;
    float fadeTimer;
} VC_FirePhoenixInst;

typedef struct {
    bool active;
    int id;
    Vector3 center;
    float radius;
    float elapsed;
    float duration;
    float pulse01;
    bool stopping;
    float fadeTimer;
} VC_FireFormationInst;

static VC_FireKickArcInst   s_fireKickArcs[VC_FIRE_KICK_MAX] = {0};
static VC_FireCrescentInst  s_fireCrescents[VC_FIRE_CRESCENT_MAX] = {0};
static VC_FireCycloneInst   s_fireCyclones[VC_FIRE_CYCLONE_MAX] = {0};
static VC_FirePhoenixInst   s_firePhoenixes[VC_FIRE_PHOENIX_MAX] = {0};
static VC_FireFormationInst s_fireFormations[VC_FIRE_FORMATION_MAX] = {0};
static int s_fireMartialNextId = 1;

static void FireMartial_DrawDualRibbon(const RibbonPoint *bodyPts, const RibbonPoint *corePts,
                                       int count, Camera3D cam, RibbonMode mode, Vector3 normal)
{
    Texture2D tex = MetalSlash_GetMask();
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        DrawRibbonStripEx(bodyPts, count, tex, cam, mode, normal);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&scope);
    }
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        DrawRibbonStripEx(corePts, count, tex, cam, mode, normal);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&scope);
    }
}

int VFX_FireKickArc_Spawn(Vector3 origin, Vector3 dir, Vector3 planeNormal,
                          float radius, float arcDeg, float duration, float scale)
{
    int slot = -1;
    for (int i = 0; i < VC_FIRE_KICK_MAX; i++) {
        if (!s_fireKickArcs[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    Vector3 d = (Vector3LengthSqr(dir) > 1e-5f) ? Vector3Normalize(dir) : (Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 n = (Vector3LengthSqr(planeNormal) > 1e-5f) ? Vector3Normalize(planeNormal) : (Vector3){ 0.0f, 1.0f, 0.0f };
    n = Vector3Subtract(n, Vector3Scale(d, Vector3DotProduct(n, d)));
    n = (Vector3LengthSqr(n) > 1e-5f) ? Vector3Normalize(n) : (Vector3){ 0.0f, 1.0f, 0.0f };

    VC_FireKickArcInst *inst = &s_fireKickArcs[slot];
    inst->active      = true;
    inst->origin      = origin;
    inst->dir         = d;
    inst->planeNormal = n;
    inst->radius      = (radius > 0.2f) ? radius : 1.65f;
    inst->arcRad      = Clamp(fabsf(arcDeg), 40.0f, 260.0f) * DEG2RAD * ((arcDeg < 0.0f) ? -1.0f : 1.0f);
    inst->elapsed     = 0.0f;
    inst->duration    = (duration > 0.06f) ? duration : 0.28f;
    inst->scale       = (scale > 0.1f) ? scale : 1.0f;
    return slot + 1;
}

int VFX_FireCrescentWave_Spawn(Vector3 origin, Vector3 dir, float range,
                               float coneDeg, float duration, float scale)
{
    int slot = -1;
    for (int i = 0; i < VC_FIRE_CRESCENT_MAX; i++) {
        if (!s_fireCrescents[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    Vector3 d = { dir.x, 0.0f, dir.z };
    d = (Vector3LengthSqr(d) > 1e-5f) ? Vector3Normalize(d) : (Vector3){ 0.0f, 0.0f, 1.0f };
    VC_FireCrescentInst *inst = &s_fireCrescents[slot];
    inst->active   = true;
    inst->origin   = origin;
    inst->dir      = d;
    inst->range    = (range > 0.5f) ? range : 5.6f;
    inst->coneRad  = Clamp(coneDeg, 40.0f, 140.0f) * DEG2RAD;
    inst->elapsed  = 0.0f;
    inst->duration = (duration > 0.08f) ? duration : 0.38f;
    inst->scale    = (scale > 0.1f) ? scale : 1.0f;
    return slot + 1;
}

int VFX_FireCyclone_Spawn(Vector3 center, float radius, float height, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_FIRE_CYCLONE_MAX; i++) {
        if (!s_fireCyclones[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    VC_FireCycloneInst *inst = &s_fireCyclones[slot];
    inst->active   = true;
    inst->id       = s_fireMartialNextId++;
    inst->center   = center;
    inst->radius   = (radius > 0.3f) ? radius : 1.85f;
    inst->height   = (height > 0.5f) ? height : 2.35f;
    inst->elapsed  = 0.0f;
    inst->duration = (duration > 0.1f) ? duration : 0.65f;
    return inst->id;
}

void VFX_FireCyclone_SetCenter(int handle, Vector3 center)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_CYCLONE_MAX; i++) {
        if (s_fireCyclones[i].active && s_fireCyclones[i].id == handle) {
            s_fireCyclones[i].center = center;
            return;
        }
    }
}

int VFX_FirePhoenixWard_Spawn(Vector3 center, float radius, int featherCount, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        if (!s_firePhoenixes[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    VC_FirePhoenixInst *inst = &s_firePhoenixes[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active       = true;
    inst->id           = s_fireMartialNextId++;
    inst->center       = center;
    inst->radius       = (radius > 0.3f) ? radius : 0.95f;
    inst->feathersLeft = (featherCount > 0) ? featherCount : 3;
    inst->duration     = (duration > 0.2f) ? duration : 4.0f;
    return inst->id;
}

void VFX_FirePhoenixWard_SetCenter(int handle, Vector3 center)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        if (s_firePhoenixes[i].active && s_firePhoenixes[i].id == handle) {
            s_firePhoenixes[i].center = center;
            return;
        }
    }
}

void VFX_FirePhoenixWard_ExpendFeather(int handle, Vector3 hitPos)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        if (s_firePhoenixes[i].active && s_firePhoenixes[i].id == handle) {
            if (s_firePhoenixes[i].feathersLeft > 0) s_firePhoenixes[i].feathersLeft--;
            Vector3 n = Vector3Subtract(hitPos, s_firePhoenixes[i].center);
            VFX_ComposeDeflectFlash(hitPos, n, VC_MAT_FIRE, 1.05f);
            VFX_ComposeEmberBurst(hitPos, n, VC_MAT_FIRE, 0.85f, 0.90f);
            return;
        }
    }
}

void VFX_FirePhoenixWard_Stop(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        if (s_firePhoenixes[i].active && s_firePhoenixes[i].id == handle) {
            s_firePhoenixes[i].stopping  = true;
            s_firePhoenixes[i].fadeTimer = 0.25f;
            return;
        }
    }
}

int VFX_FireScorchFormation_Spawn(Vector3 center, float radius, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_FIRE_FORMATION_MAX; i++) {
        if (!s_fireFormations[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    VC_FireFormationInst *inst = &s_fireFormations[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active   = true;
    inst->id       = s_fireMartialNextId++;
    inst->center   = center;
    inst->radius   = (radius > 0.5f) ? radius : 3.5f;
    inst->duration = (duration > 0.2f) ? duration : 5.0f;
    inst->pulse01  = 1.0f;
    VFX_ComposeDecalVariant(center, VC_MAT_FIRE, inst->radius * 0.92f, 0.72f, inst->duration / 4.0f, VFX_DECAL_VARIANT_SCORCH);
    VFX_SpawnGroundWave(center, VC_MAT_FIRE, inst->radius, 0.36f);
    return inst->id;
}

void VFX_FireScorchFormation_SetPulse(int handle, float pulse01)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_FORMATION_MAX; i++) {
        if (s_fireFormations[i].active && s_fireFormations[i].id == handle) {
            s_fireFormations[i].pulse01 = Clamp(pulse01, 0.0f, 1.0f);
            if (pulse01 >= 0.85f) {
                Wind_SpawnRadialBlast(s_fireFormations[i].center, s_fireFormations[i].radius * 1.1f, 3.2f, 0.18f);
            }
            return;
        }
    }
}

void VFX_FireScorchFormation_Stop(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_FIRE_FORMATION_MAX; i++) {
        if (s_fireFormations[i].active && s_fireFormations[i].id == handle) {
            s_fireFormations[i].stopping  = true;
            s_fireFormations[i].fadeTimer = 0.35f;
            return;
        }
    }
}

static void VC_FireMartial_Update(float dt)
{
    for (int i = 0; i < VC_FIRE_KICK_MAX; i++) {
        if (!s_fireKickArcs[i].active) continue;
        s_fireKickArcs[i].elapsed += dt;
        if (s_fireKickArcs[i].elapsed >= s_fireKickArcs[i].duration) s_fireKickArcs[i].active = false;
    }
    for (int i = 0; i < VC_FIRE_CRESCENT_MAX; i++) {
        if (!s_fireCrescents[i].active) continue;
        s_fireCrescents[i].elapsed += dt;
        if (s_fireCrescents[i].elapsed >= s_fireCrescents[i].duration) s_fireCrescents[i].active = false;
    }
    for (int i = 0; i < VC_FIRE_CYCLONE_MAX; i++) {
        if (!s_fireCyclones[i].active) continue;
        s_fireCyclones[i].elapsed += dt;
        if (s_fireCyclones[i].elapsed >= s_fireCyclones[i].duration) s_fireCyclones[i].active = false;
    }
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        VC_FirePhoenixInst *inst = &s_firePhoenixes[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping = true;
            inst->fadeTimer = 0.25f;
        }
    }
    for (int i = 0; i < VC_FIRE_FORMATION_MAX; i++) {
        VC_FireFormationInst *inst = &s_fireFormations[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        inst->pulse01 = fmaxf(0.0f, inst->pulse01 - dt * 1.8f);
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping = true;
            inst->fadeTimer = 0.35f;
        }
    }
}

static void VC_FireMartial_Draw3D(Camera3D cam)
{
    static RibbonPoint bodyPts[24];
    static RibbonPoint corePts[24];
    const Color sootDark = { 195, 42, 10, 255 };
    const Color flameHot = { 255, 195, 78, 255 };

    /* 1. Swept Fire Kick Arcs (clean translucent crimson-amber outer glow + hot core) */
    for (int i = 0; i < VC_FIRE_KICK_MAX; i++) {
        const VC_FireKickArcInst *inst = &s_fireKickArcs[i];
        if (!inst->active) continue;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float headU = 1.0f - powf(1.0f - Clamp(t01 / 0.42f, 0.0f, 1.0f), 2.0f);
        float tailU = Clamp((t01 - 0.16f) / 0.84f, 0.0f, 1.0f);
        if (headU - tailU <= 0.03f) continue;
        float fade = powf(1.0f - t01, 0.65f);

        Vector3 right = Vector3Normalize(Vector3CrossProduct(inst->planeNormal, inst->dir));
        const int N = 20;
        for (int k = 0; k < N; k++) {
            float s = (float)k / (float)(N - 1);
            float u = tailU + (headU - tailU) * s;
            float ang = (u - 0.5f) * inst->arcRad;
            Vector3 radial = Vector3Add(Vector3Scale(inst->dir, cosf(ang)), Vector3Scale(right, sinf(ang)));
            Vector3 p = Vector3Add(inst->origin, Vector3Scale(radial, inst->radius * inst->scale));
            float taper = powf(s, 0.55f) * sinf(s * PI);
            bodyPts[k].position  = p;
            bodyPts[k].halfWidth = 0.34f * inst->scale * taper;
            bodyPts[k].tint      = ColorAlpha(sootDark, Clamp(0.48f * fade * s, 0.0f, 1.0f));
            bodyPts[k].v         = s;
            corePts[k].position  = Vector3Add(p, Vector3Scale(radial, 0.08f * inst->scale));
            corePts[k].halfWidth = 0.16f * inst->scale * taper;
            corePts[k].tint      = VC_Premultiply(flameHot, Clamp(0.96f * fade * s, 0.0f, 1.0f));
            corePts[k].v         = s;
        }
        FireMartial_DrawDualRibbon(bodyPts, corePts, N, cam, RIBBON_FIXED_NORMAL, inst->planeNormal);
    }

    /* 2. Continuous 110-deg Advancing Fire Crescent Wave (Viêm Hồ Bán Nguyệt Cước) */
    for (int i = 0; i < VC_FIRE_CRESCENT_MAX; i++) {
        const VC_FireCrescentInst *inst = &s_fireCrescents[i];
        if (!inst->active) continue;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float ease = 1.0f - powf(1.0f - t01, 2.0f);
        float dist = 0.75f + (inst->range - 0.75f) * ease;
        float fade = (t01 < 0.72f) ? 1.0f : (1.0f - (t01 - 0.72f) / 0.28f);
        Vector3 right = { -inst->dir.z, 0.0f, inst->dir.x };
        const int N = 22;
        for (int k = 0; k < N; k++) {
            float s = (float)k / (float)(N - 1);
            float ang = (s - 0.5f) * inst->coneRad;
            Vector3 radial = Vector3Add(Vector3Scale(inst->dir, cosf(ang)), Vector3Scale(right, sinf(ang)));
            Vector3 p = Vector3Add(inst->origin, Vector3Scale(radial, dist));
            p.y = VFX_GroundHeightFromMap(p.x, p.z, NULL) + 0.78f;
            float arch = sinf(s * PI);
            bodyPts[k].position  = Vector3Subtract(p, Vector3Scale(radial, 0.18f));
            bodyPts[k].halfWidth = 0.42f * inst->scale * arch;
            bodyPts[k].tint      = ColorAlpha(sootDark, Clamp(0.50f * fade * arch, 0.0f, 1.0f));
            bodyPts[k].v         = s;
            corePts[k].position  = p;
            corePts[k].halfWidth = 0.20f * inst->scale * arch;
            corePts[k].tint      = VC_Premultiply(flameHot, Clamp(0.98f * fade * arch, 0.0f, 1.0f));
            corePts[k].v         = s;
        }
        FireMartial_DrawDualRibbon(bodyPts, corePts, N, cam, RIBBON_FIXED_NORMAL, (Vector3){ 0.0f, 1.0f, 0.0f });
    }

    /* 3. Hollow-Center 3D Helical Fire Cyclone (Phần Thiên Toàn Phong Cước) */
    for (int i = 0; i < VC_FIRE_CYCLONE_MAX; i++) {
        const VC_FireCycloneInst *inst = &s_fireCyclones[i];
        if (!inst->active) continue;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float fade = sinf(t01 * PI);
        const int N = 24;
        for (int strand = 0; strand < 2; strand++) {
            float baseAng = inst->elapsed * 14.0f + (float)strand * PI;
            for (int k = 0; k < N; k++) {
                float u = (float)k / (float)(N - 1);
                float ang = baseAng + u * 4.5f * PI;
                float r = inst->radius * (0.48f + 0.58f * u);
                Vector3 p = {
                    inst->center.x + cosf(ang) * r,
                    inst->center.y + 0.12f + u * inst->height,
                    inst->center.z + sinf(ang) * r
                };
                float env = sinf(u * PI);
                bodyPts[k].position  = p;
                bodyPts[k].halfWidth = 0.26f * env;
                bodyPts[k].tint      = ColorAlpha(sootDark, Clamp(0.44f * fade * env, 0.0f, 1.0f));
                bodyPts[k].v         = u;
                corePts[k].position  = p;
                corePts[k].halfWidth = 0.12f * env;
                corePts[k].tint      = VC_Premultiply(flameHot, Clamp(0.95f * fade * env, 0.0f, 1.0f));
                corePts[k].v         = u;
            }
            FireMartial_DrawDualRibbon(bodyPts, corePts, N, cam, RIBBON_CAMERA_FACING, (Vector3){ 0.0f, 1.0f, 0.0f });
        }
    }

    /* 4. 3D Sculpted Orbiting Phoenix Feathers (Hỏa Vũ Hồi Phong Cước) */
    for (int i = 0; i < VC_FIRE_PHOENIX_MAX; i++) {
        const VC_FirePhoenixInst *inst = &s_firePhoenixes[i];
        if (!inst->active || inst->feathersLeft <= 0) continue;
        float vis = inst->stopping ? Clamp(inst->fadeTimer / 0.25f, 0.0f, 1.0f) : Clamp(inst->elapsed / 0.18f, 0.0f, 1.0f);
        for (int k = 0; k < inst->feathersLeft; k++) {
            float ang = inst->elapsed * 2.2f + (float)k * (2.0f * PI / (float)inst->feathersLeft);
            Vector3 fp = {
                inst->center.x + cosf(ang) * inst->radius,
                inst->center.y + 1.08f + 0.09f * sinf(inst->elapsed * 4.5f + (float)k),
                inst->center.z + sinf(ang) * inst->radius
            };
            VC_DrawMarkSigil3D(fp, 0.075f * vis, 0.28f * vis, ang + 0.6f,
                               (Color){ 155, 24, 14, (unsigned char)(235 * vis) },
                               (Color){ 255, 205, 75, (unsigned char)(220 * vis) });
        }
    }

    /* 5. Ground-Hugging Scorching Trigram Formation (Chước Địa Hỏa Trận, C11) */
    for (int i = 0; i < VC_FIRE_FORMATION_MAX; i++) {
        const VC_FireFormationInst *inst = &s_fireFormations[i];
        if (!inst->active) continue;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float vis = inst->stopping ? Clamp(inst->fadeTimer / 0.35f, 0.0f, 1.0f) : Clamp(inst->elapsed / 0.25f, 0.0f, 1.0f);
        Vector3 gp = { inst->center.x, VFX_GroundHeightFromMap(inst->center.x, inst->center.z, NULL), inst->center.z };
        VFX_ComposeRuneCircle(gp, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, inst->radius, t01, 2);
        /* 8 low obsidian-ember rim markers (y <= 0.16m) */
        for (int k = 0; k < 8; k++) {
            float ang = (float)k * (2.0f * PI / 8.0f);
            float bx = inst->center.x + cosf(ang) * inst->radius * 0.96f;
            float bz = inst->center.z + sinf(ang) * inst->radius * 0.96f;
            float by = VFX_GroundHeightFromMap(bx, bz, NULL) + 0.08f;
            VC_DrawMarkSigil3D((Vector3){ bx, by, bz }, 0.08f * vis, 0.08f * vis, ang,
                               (Color){ 36, 20, 16, (unsigned char)(225 * vis) },
                               (Color){ 255, 130, 35, (unsigned char)((120 + 90 * inst->pulse01) * vis) });
        }
    }
}
