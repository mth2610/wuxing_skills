// ── Water Martial Primitive: Orbiting Octagonal Water-Mirror Ward ────────────
// Represents deflect charges (2 -> 1 -> 0) for Thủy Kính Quyền Thuẫn with crisp
// dual-pass sapphire glass BODY + specular rim EMISSION panes.

#define VC_WATER_MIRROR_MAX 6

typedef struct {
    bool active;
    int id;
    Vector3 center;
    float radius;
    int mirrorsLeft;
    float elapsed;
    float duration;
    bool stopping;
    float fadeTimer;
} VC_WaterMirrorInst;

static VC_WaterMirrorInst s_waterMirrors[VC_WATER_MIRROR_MAX] = {0};
static int s_waterMirrorNextId = 1;

int VFX_WaterMirror_Spawn(Vector3 center, float radius, int mirrors, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        if (!s_waterMirrors[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    VC_WaterMirrorInst *inst = &s_waterMirrors[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active      = true;
    inst->id          = s_waterMirrorNextId++;
    inst->center      = center;
    inst->radius      = (radius > 0.3f) ? radius : 1.15f;
    inst->mirrorsLeft = (mirrors > 0) ? mirrors : 2;
    inst->duration    = (duration > 0.2f) ? duration : 4.0f;
    return inst->id;
}

void VFX_WaterMirror_SetCenter(int handle, Vector3 center)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        if (s_waterMirrors[i].active && s_waterMirrors[i].id == handle) {
            s_waterMirrors[i].center = center;
            return;
        }
    }
}

void VFX_WaterMirror_ShatterOne(int handle, Vector3 hitPos)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        if (s_waterMirrors[i].active && s_waterMirrors[i].id == handle) {
            if (s_waterMirrors[i].mirrorsLeft > 0) s_waterMirrors[i].mirrorsLeft--;
            Vector3 n = Vector3Subtract(hitPos, s_waterMirrors[i].center);
            VFX_ComposeDeflectFlash(hitPos, n, VC_MAT_WATER, 1.05f);
            VFX_CrystalShatter_Spawn(hitPos, n, 4.2f, 8, 0.11f, VFX_CRYSTAL_STYLE_ICE);
            return;
        }
    }
}

void VFX_WaterMirror_Stop(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        if (s_waterMirrors[i].active && s_waterMirrors[i].id == handle) {
            s_waterMirrors[i].stopping  = true;
            s_waterMirrors[i].fadeTimer = 0.25f;
            return;
        }
    }
}

static void VC_WaterMirror_Update(float dt)
{
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        VC_WaterMirrorInst *inst = &s_waterMirrors[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping  = true;
            inst->fadeTimer = 0.25f;
        }
    }
}

static void VC_WaterMirror_Draw3D(Camera3D cam)
{
    (void)cam;
    for (int i = 0; i < VC_WATER_MIRROR_MAX; i++) {
        const VC_WaterMirrorInst *inst = &s_waterMirrors[i];
        if (!inst->active || inst->mirrorsLeft <= 0) continue;
        float vis = inst->stopping
            ? Clamp(inst->fadeTimer / 0.25f, 0.0f, 1.0f)
            : Clamp(inst->elapsed / 0.18f, 0.0f, 1.0f);
        if (vis <= 0.01f) continue;

        for (int k = 0; k < inst->mirrorsLeft; k++) {
            float ang = inst->elapsed * 1.45f + (float)k * (2.0f * PI / (float)inst->mirrorsLeft);
            Vector3 radial = { cosf(ang), 0.0f, sinf(ang) };
            Vector3 tang   = { -sinf(ang), 0.0f, cosf(ang) };
            Vector3 up     = { 0.0f, 1.0f, 0.0f };
            Vector3 mp     = {
                inst->center.x + radial.x * inst->radius,
                inst->center.y + 1.05f + 0.06f * sinf(inst->elapsed * 3.0f + (float)k),
                inst->center.z + radial.z * inst->radius
            };
            float paneR = 0.36f * vis;

            for (int pass = 0; pass < 2; pass++) {
                VFXRenderScope scope = VFXRender_BeginDraw(
                    pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
                    pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_PREMULTIPLIED,
                    false);
                rlDrawRenderBatchActive();
                rlDisableBackfaceCulling();
                rlSetTexture(0);
                rlBegin(RL_TRIANGLES);
                Color c = (pass == 0)
                    ? ColorAlpha((Color){ 20, 95, 148, 255 }, 0.52f * vis)
                    : VC_Premultiply((Color){ 170, 238, 255, 255 }, 0.78f * vis);
                rlColor4ub(c.r, c.g, c.b, c.a);
                for (int s = 0; s < 8; s++) {
                    float a0 = (float)s * (2.0f * PI / 8.0f);
                    float a1 = (float)(s + 1) * (2.0f * PI / 8.0f);
                    float rIn = (pass == 0) ? 0.0f : paneR * 0.80f;
                    Vector3 p0o = Vector3Add(mp, Vector3Add(Vector3Scale(tang, cosf(a0) * paneR), Vector3Scale(up, sinf(a0) * paneR * 1.25f)));
                    Vector3 p1o = Vector3Add(mp, Vector3Add(Vector3Scale(tang, cosf(a1) * paneR), Vector3Scale(up, sinf(a1) * paneR * 1.25f)));
                    if (pass == 0) {
                        rlVertex3f(mp.x, mp.y, mp.z);
                        rlVertex3f(p0o.x, p0o.y, p0o.z);
                        rlVertex3f(p1o.x, p1o.y, p1o.z);
                    } else {
                        Vector3 p0i = Vector3Add(mp, Vector3Add(Vector3Scale(tang, cosf(a0) * rIn), Vector3Scale(up, sinf(a0) * rIn * 1.25f)));
                        Vector3 p1i = Vector3Add(mp, Vector3Add(Vector3Scale(tang, cosf(a1) * rIn), Vector3Scale(up, sinf(a1) * rIn * 1.25f)));
                        rlVertex3f(p0i.x, p0i.y, p0i.z); rlVertex3f(p0o.x, p0o.y, p0o.z); rlVertex3f(p1o.x, p1o.y, p1o.z);
                        rlVertex3f(p0i.x, p0i.y, p0i.z); rlVertex3f(p1o.x, p1o.y, p1o.z); rlVertex3f(p1i.x, p1i.y, p1i.z);
                    }
                }
                rlEnd();
                rlDrawRenderBatchActive();
                rlEnableBackfaceCulling();
                VFXRender_EndDraw(&scope);
            }
        }
    }
}
