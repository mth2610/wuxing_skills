// vc_metal_parry_guard.inl — Tier-1 Elastic Sword-Qi Parry Guard (Kim Cang Phản Chấn)
// Included once via metal/metal.inl -> visual_composer.c (after vc_metal_blades.inl)
//
// Torso-layer (y ~ 1.05m), semi-transparent (alpha <= 0.35), zero permanent bloom.
// Flashes bright sparks + oriented shock ring ONLY when deflecting an enemy projectile.

#ifndef VC_METAL_PARRY_GUARD_INL
#define VC_METAL_PARRY_GUARD_INL

#ifndef PI
#define PI 3.1415926535f
#endif

#define VC_PARRY_MAX 8

typedef struct {
    bool    active;
    int     id;
    Vector3 pos;
    Vector3 forward;
    float   radius;
    float   elapsed;
    float   duration;
    bool    stopping;
    float   fadeTimer;
    float   deflectFlash;
    Vector3 deflectPos;
} VC_MetalParryInst;

static VC_MetalParryInst s_parryGuards[VC_PARRY_MAX];
static int               s_parryNextId = 1;

static VC_MetalParryInst *ParryGuard_Find(int handle)
{
    if (handle <= 0) return NULL;
    for (int i = 0; i < VC_PARRY_MAX; i++) {
        if (s_parryGuards[i].active && s_parryGuards[i].id == handle) return &s_parryGuards[i];
    }
    return NULL;
}

int VFX_MetalParryGuard_Spawn(Vector3 pos, float radius, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_PARRY_MAX; i++) {
        if (!s_parryGuards[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_MetalParryInst *inst = &s_parryGuards[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active   = true;
    inst->id       = s_parryNextId++;
    inst->pos      = pos;
    inst->forward  = (Vector3){ 0.0f, 0.0f, 1.0f };
    inst->radius   = (radius > 0.3f) ? radius : 1.8f;
    inst->duration = (duration > 0.2f) ? duration : 4.0f;
    return inst->id;
}

void VFX_MetalParryGuard_SetTransform(int handle, Vector3 pos, Vector3 forward)
{
    VC_MetalParryInst *inst = ParryGuard_Find(handle);
    if (!inst) return;
    inst->pos = pos;
    if (Vector3LengthSqr(forward) > 1e-5f) inst->forward = Vector3Normalize(forward);
}

void VFX_MetalParryGuard_TriggerDeflect(int handle, Vector3 deflectPos)
{
    VC_MetalParryInst *inst = ParryGuard_Find(handle);
    if (!inst) return;
    inst->deflectFlash = 1.0f;
    inst->deflectPos   = deflectPos;
    VFX_ComposeContactSparkMode(deflectPos, VC_MAT_METAL, 0.90f, 0.95f, CONTACT_SPARK_CENTRIFUGAL);
    Wind_SpawnRadialBlast(deflectPos, 1.8f, 6.0f, 0.18f);
}

void VFX_MetalParryGuard_Stop(int handle)
{
    VC_MetalParryInst *inst = ParryGuard_Find(handle);
    if (inst && !inst->stopping) {
        inst->stopping = true;
        inst->fadeTimer = 0.22f;
    }
}

void VFX_MetalParryGuard_Kill(int handle)
{
    VC_MetalParryInst *inst = ParryGuard_Find(handle);
    if (inst) inst->active = false;
}

void VC_MetalParryGuard_Update(float dt)
{
    for (int i = 0; i < VC_PARRY_MAX; i++) {
        VC_MetalParryInst *inst = &s_parryGuards[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (inst->deflectFlash > 0.0f) {
            inst->deflectFlash = fmaxf(0.0f, inst->deflectFlash - dt * 3.8f);
        }
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping = true;
            inst->fadeTimer = 0.22f;
        }
    }
}

void VC_MetalParryGuard_Draw3D(Camera3D cam)
{
    Texture2D tex = MetalSlash_GetMask();
    for (int i = 0; i < VC_PARRY_MAX; i++) {
        const VC_MetalParryInst *inst = &s_parryGuards[i];
        if (!inst->active) continue;

        float fade = inst->stopping ? fmaxf(0.0f, inst->fadeTimer / 0.22f)
                                    : fminf(1.0f, inst->elapsed / 0.14f);
        if (fade <= 0.02f) continue;

        Vector3 center = { inst->pos.x, inst->pos.y + 1.05f, inst->pos.z };
        float rBase = inst->radius * (1.0f + 0.05f * inst->deflectFlash);

        /* 2 slender tilted semi-transparent sword-qi guard arcs */
        static const float tilts[2]  = { 0.26f, -0.32f };
        static const float speeds[2] = { 4.2f, -3.4f };

        for (int ring = 0; ring < 2; ring++) {
            float spin = inst->elapsed * speeds[ring];
            Vector3 normal = Vector3Normalize((Vector3){ sinf(spin) * tilts[ring], 1.0f, cosf(spin) * tilts[ring] });
            Vector3 axU, axV;
            VC_PlaneFrame(normal, &axU, &axV);

            static RibbonPoint bodyPts[28];
            static RibbonPoint edgePts[28];
            float arcSpan = 1.45f * PI;
            float aStart  = spin + (float)ring * 2.1f;
            float aNorm   = fade * (0.32f + 0.45f * inst->deflectFlash);

            for (int p = 0; p < 28; p++) {
                float u = (float)p / 27.0f;
                float a = aStart + arcSpan * u;
                float env = powf(sinf(u * PI), 0.70f);
                Vector3 wp = Vector3Add(center,
                                        Vector3Add(Vector3Scale(axU, cosf(a) * rBase),
                                                   Vector3Scale(axV, sinf(a) * rBase)));
                bodyPts[p].position  = wp;
                bodyPts[p].halfWidth = 0.055f * env;
                bodyPts[p].tint      = ColorAlpha((Color){ 48, 58, 72, 255 }, Clamp(aNorm, 0.0f, 1.0f));
                bodyPts[p].v         = u;

                edgePts[p].position  = wp;
                edgePts[p].halfWidth = 0.018f * env;
                edgePts[p].tint      = VC_Premultiply((Color){ 210, 232, 255, 255 }, Clamp(aNorm * 0.85f, 0.0f, 1.0f));
                edgePts[p].v         = u;
            }
            {
                VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
                rlDrawRenderBatchActive();
                rlDisableBackfaceCulling();
                rlDrawRenderBatchActive();
                DrawRibbonStripEx(bodyPts, 28, tex, cam, RIBBON_FIXED_NORMAL, normal);
                rlDrawRenderBatchActive();
                rlEnableBackfaceCulling();
                rlDrawRenderBatchActive();
                VFXRender_EndDraw(&scope);
            }
            if (inst->deflectFlash > 0.05f || ring == 0) {
                VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
                rlDrawRenderBatchActive();
                rlDisableBackfaceCulling();
                rlDrawRenderBatchActive();
                DrawRibbonStripEx(edgePts, 28, tex, cam, RIBBON_FIXED_NORMAL, normal);
                rlDrawRenderBatchActive();
                rlEnableBackfaceCulling();
                rlDrawRenderBatchActive();
                VFXRender_EndDraw(&scope);
            }
        }

        /* Reactive oriented shock ring only on projectile deflect */
        if (inst->deflectFlash > 0.05f) {
            Vector3 toHit = Vector3Subtract(inst->deflectPos, center);
            if (Vector3LengthSqr(toHit) < 1e-4f) toHit = inst->forward;
            VFX_ComposeShockRing(inst->deflectPos, Vector3Normalize(toHit), VC_MAT_METAL, 1.35f, 1.0f - inst->deflectFlash);
        }
    }
}

#endif /* VC_METAL_PARRY_GUARD_INL */
