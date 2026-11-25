// ── Water Martial Primitive: Dynamic Hydro Whip Tether (Nghịch Lưu Cầm Nã) ──
// Dual-pass Beer-Lambert deep-sapphire BODY + Fresnel cyan EMISSION ribbon whip
// connecting caster hand to target/projectile with sinusoidal whip oscillation.

#define VC_HYDRO_TETHER_MAX 8

typedef struct {
    bool active;
    int id;
    Vector3 from;
    Vector3 to;
    float thickness;
    float elapsed;
    float duration;
    bool stopping;
    float fadeTimer;
} VC_HydroTetherInst;

static VC_HydroTetherInst s_hydroTethers[VC_HYDRO_TETHER_MAX] = {0};
static int s_hydroTetherNextId = 1;

int VFX_HydroTether_Spawn(Vector3 from, Vector3 to, float thickness, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_HYDRO_TETHER_MAX; i++) {
        if (!s_hydroTethers[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    VC_HydroTetherInst *inst = &s_hydroTethers[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active    = true;
    inst->id        = s_hydroTetherNextId++;
    inst->from      = from;
    inst->to        = to;
    inst->thickness = (thickness > 0.02f) ? thickness : 0.14f;
    inst->duration  = (duration > 0.08f) ? duration : 0.85f;
    return inst->id;
}

void VFX_HydroTether_SetEndpoints(int handle, Vector3 from, Vector3 to)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_HYDRO_TETHER_MAX; i++) {
        if (s_hydroTethers[i].active && s_hydroTethers[i].id == handle) {
            s_hydroTethers[i].from = from;
            s_hydroTethers[i].to   = to;
            return;
        }
    }
}

void VFX_HydroTether_Kill(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_HYDRO_TETHER_MAX; i++) {
        if (s_hydroTethers[i].active && s_hydroTethers[i].id == handle) {
            s_hydroTethers[i].stopping  = true;
            s_hydroTethers[i].fadeTimer = 0.16f;
            return;
        }
    }
}

static void VC_HydroTether_Update(float dt)
{
    for (int i = 0; i < VC_HYDRO_TETHER_MAX; i++) {
        VC_HydroTetherInst *inst = &s_hydroTethers[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->active = false;
        }
    }
}

static void VC_HydroTether_Draw3D(Camera3D cam)
{
    static RibbonPoint bodyPts[20];
    static RibbonPoint glowPts[20];
    const int N = 20;

    for (int i = 0; i < VC_HYDRO_TETHER_MAX; i++) {
        const VC_HydroTetherInst *inst = &s_hydroTethers[i];
        if (!inst->active) continue;
        float dist = Vector3Distance(inst->from, inst->to);
        if (dist < 0.08f) continue;

        float fade = inst->stopping
            ? Clamp(inst->fadeTimer / 0.16f, 0.0f, 1.0f)
            : (1.0f - SmoothStep01((inst->elapsed / inst->duration - 0.78f) / 0.22f));
        if (fade <= 0.01f) continue;

        Vector3 dir = Vector3Scale(Vector3Subtract(inst->to, inst->from), 1.0f / dist);
        Vector3 right = Vector3Normalize((Vector3){ -dir.z, 0.0f, dir.x });
        if (Vector3LengthSqr(right) < 1e-4f) right = (Vector3){ 1.0f, 0.0f, 0.0f };
        Vector3 up = (Vector3){ 0.0f, 1.0f, 0.0f };

        float waveAmp = fminf(0.32f, dist * 0.09f) * fade;
        for (int k = 0; k < N; k++) {
            float u = (float)k / (float)(N - 1);
            float arch = sinf(u * PI);
            float whip = sinf(u * 9.5f - inst->elapsed * 18.0f) * waveAmp * arch;
            float lift = (0.16f + 0.10f * cosf(u * 6.5f - inst->elapsed * 14.0f)) * arch;
            Vector3 p = Vector3Lerp(inst->from, inst->to, u);
            p = Vector3Add(p, Vector3Scale(right, whip));
            p = Vector3Add(p, Vector3Scale(up, lift));

            float taper = (0.35f + 0.65f * arch) * (1.0f - 0.35f * u);
            bodyPts[k].position  = p;
            bodyPts[k].halfWidth = inst->thickness * taper;
            bodyPts[k].tint      = ColorAlpha((Color){ 14, 72, 118, 255 }, Clamp(0.86f * fade, 0.0f, 1.0f));
            bodyPts[k].v         = u;

            glowPts[k].position  = p;
            glowPts[k].halfWidth = inst->thickness * 0.45f * taper;
            glowPts[k].tint      = VC_Premultiply((Color){ 145, 232, 255, 255 }, Clamp(0.92f * fade, 0.0f, 1.0f));
            glowPts[k].v         = u;
        }

        {
            VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            DrawRibbonStrip(bodyPts, N, (Texture2D){0}, cam);
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            VFXRender_EndDraw(&scope);
        }
        {
            VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            DrawRibbonStrip(glowPts, N, (Texture2D){0}, cam);
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            VFXRender_EndDraw(&scope);
        }
    }
}
