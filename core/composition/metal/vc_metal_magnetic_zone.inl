// vc_metal_magnetic_zone.inl — Tier-1 Ground-Layer Magnetic Domain (Canh Kim Kiếm Vực)
// Included once via metal/metal.inl -> visual_composer.c (after vc_metal_blades.inl)
//
// C11 Compliant:
//   - Strictly ground-hugging: y <= 0.20m above terrain (anchor hilts <= 0.14m)
//   - Translucent interior & geometric magnetic dipole flux lines: alpha <= 0.30
//   - Never obscures character silhouettes.

#ifndef VC_METAL_MAGNETIC_ZONE_INL
#define VC_METAL_MAGNETIC_ZONE_INL

#ifndef PI
#define PI 3.1415926535f
#endif

#define VC_MAG_ZONE_MAX 8

typedef struct {
    bool    active;
    int     id;
    Vector3 center;
    float   radius;
    float   elapsed;
    float   duration;
    float   pulse01;
    bool    stopping;
    float   fadeTimer;
} VC_MetalMagZoneInst;

static VC_MetalMagZoneInst s_magZones[VC_MAG_ZONE_MAX];
static int                 s_magZoneNextId = 1;

static VC_MetalMagZoneInst *MagZone_Find(int handle)
{
    if (handle <= 0) return NULL;
    for (int i = 0; i < VC_MAG_ZONE_MAX; i++) {
        if (s_magZones[i].active && s_magZones[i].id == handle) return &s_magZones[i];
    }
    return NULL;
}

int VFX_MetalMagneticZone_Spawn(Vector3 center, float radius, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_MAG_ZONE_MAX; i++) {
        if (!s_magZones[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_MetalMagZoneInst *inst = &s_magZones[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active   = true;
    inst->id       = s_magZoneNextId++;
    inst->center   = center;
    inst->radius   = (radius > 0.5f) ? radius : 3.8f;
    inst->duration = (duration > 0.2f) ? duration : 5.0f;
    inst->pulse01  = 1.0f;
    return inst->id;
}

void VFX_MetalMagneticZone_SetPulse(int handle, float pulse01)
{
    VC_MetalMagZoneInst *inst = MagZone_Find(handle);
    if (!inst) return;
    inst->pulse01 = Clamp(pulse01, 0.0f, 1.0f);
    if (inst->pulse01 >= 0.85f) {
        Wind_SpawnRadialBlast(inst->center, inst->radius * 1.05f, 2.2f, 0.16f);
    }
}

void VFX_MetalMagneticZone_Stop(int handle)
{
    VC_MetalMagZoneInst *inst = MagZone_Find(handle);
    if (inst && !inst->stopping) {
        inst->stopping  = true;
        inst->fadeTimer = 0.35f;
    }
}

void VFX_MetalMagneticZone_Kill(int handle)
{
    VC_MetalMagZoneInst *inst = MagZone_Find(handle);
    if (inst) inst->active = false;
}

void VC_MetalMagneticZone_Update(float dt)
{
    for (int i = 0; i < VC_MAG_ZONE_MAX; i++) {
        VC_MetalMagZoneInst *inst = &s_magZones[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        inst->pulse01 = fmaxf(0.0f, inst->pulse01 - dt * 1.8f);
        if (inst->stopping) {
            inst->fadeTimer -= dt;
            if (inst->fadeTimer <= 0.0f) inst->active = false;
        } else if (inst->elapsed >= inst->duration) {
            inst->stopping  = true;
            inst->fadeTimer = 0.35f;
        }
    }
}

static void MagZone_DrawGroundRing(Vector3 center, float rIn, float rOut, Color col, int segs)
{
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    for (int s = 0; s < segs; s++) {
        float a0 = (float)s / (float)segs * 2.0f * PI;
        float a1 = (float)(s + 1) / (float)segs * 2.0f * PI;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        float x0i = center.x + c0 * rIn,  z0i = center.z + s0 * rIn;
        float x1i = center.x + c1 * rIn,  z1i = center.z + s1 * rIn;
        float x0o = center.x + c0 * rOut, z0o = center.z + s0 * rOut;
        float x1o = center.x + c1 * rOut, z1o = center.z + s1 * rOut;
        float y0i = VFX_GroundHeightFromMap(x0i, z0i, NULL) + 0.03f;
        float y1i = VFX_GroundHeightFromMap(x1i, z1i, NULL) + 0.03f;
        float y0o = VFX_GroundHeightFromMap(x0o, z0o, NULL) + 0.03f;
        float y1o = VFX_GroundHeightFromMap(x1o, z1o, NULL) + 0.03f;
        rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1i, y1i, z1i); rlVertex3f(x1o, y1o, z1o);
        rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1o, y1o, z1o); rlVertex3f(x0o, y0o, z0o);
    }
    rlEnd();
}

static void MagZone_DrawFluxLine(Vector3 a, Vector3 b, float halfW, Color col)
{
    Vector3 d = { b.x - a.x, 0.0f, b.z - a.z };
    float len = Vector3Length(d);
    if (len < 1e-4f) return;
    Vector3 side = { -d.z / len * halfW, 0.0f, d.x / len * halfW };
    float ya = VFX_GroundHeightFromMap(a.x, a.z, NULL) + 0.035f;
    float yb = VFX_GroundHeightFromMap(b.x, b.z, NULL) + 0.035f;

    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(a.x - side.x, ya, a.z - side.z);
    rlVertex3f(a.x + side.x, ya, a.z + side.z);
    rlVertex3f(b.x + side.x, yb, b.z + side.z);
    rlVertex3f(a.x - side.x, ya, a.z - side.z);
    rlVertex3f(b.x + side.x, yb, b.z + side.z);
    rlVertex3f(b.x - side.x, yb, b.z - side.z);
    rlEnd();
}

void VC_MetalMagneticZone_Draw3D(Camera3D cam)
{
    (void)cam;
    for (int i = 0; i < VC_MAG_ZONE_MAX; i++) {
        const VC_MetalMagZoneInst *inst = &s_magZones[i];
        if (!inst->active) continue;

        float vis = inst->stopping ? fmaxf(0.0f, inst->fadeTimer / 0.35f)
                                   : fminf(1.0f, inst->elapsed / 0.25f);
        if (vis <= 0.01f) continue;

        float r = inst->radius;
        /* Alpha strictly <= 0.30 (76/255) for C11 ground-layer compliance */
        float aPerimeter = Clamp((0.26f + 0.04f * inst->pulse01) * vis, 0.0f, 0.30f);
        float aFlux      = Clamp((0.18f + 0.08f * inst->pulse01) * vis, 0.0f, 0.28f);

        /* Pass 1: Gunmetal magnetic grid lines on ground (Body) */
        {
            VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlDrawRenderBatchActive();
            rlSetTexture(0);

            MagZone_DrawGroundRing(inst->center, r - 0.06f, r + 0.06f,
                                   ColorAlpha((Color){ 54, 68, 86, 255 }, aPerimeter), 48);
            MagZone_DrawGroundRing(inst->center, r * 0.52f - 0.03f, r * 0.52f + 0.03f,
                                   ColorAlpha((Color){ 54, 68, 86, 255 }, aFlux), 36);

            /* 8 geometric dipole chord lines (octagram lattice, y <= 0.04m) */
            float rot = inst->elapsed * 0.14f;
            for (int k = 0; k < 8; k++) {
                float a0 = rot + (float)k * (2.0f * PI / 8.0f);
                float a1 = rot + (float)((k + 3) % 8) * (2.0f * PI / 8.0f);
                Vector3 p0 = { inst->center.x + cosf(a0) * r * 0.96f, inst->center.y, inst->center.z + sinf(a0) * r * 0.96f };
                Vector3 p1 = { inst->center.x + cosf(a1) * r * 0.96f, inst->center.y, inst->center.z + sinf(a1) * r * 0.96f };
                MagZone_DrawFluxLine(p0, p1, 0.025f, ColorAlpha((Color){ 64, 80, 102, 255 }, aFlux));
            }
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            rlDrawRenderBatchActive();
            VFXRender_EndDraw(&scope);
        }

        /* Pass 2: Subtle silver-cyan pulse shimmer on perimeter (alpha <= 0.24) */
        {
            VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlDrawRenderBatchActive();
            rlSetTexture(0);

            float pulseR = r * (0.20f + 0.80f * (1.0f - inst->pulse01));
            if (inst->pulse01 > 0.05f) {
                MagZone_DrawGroundRing(inst->center, fmaxf(0.05f, pulseR - 0.04f), pulseR + 0.04f,
                                       VC_Premultiply((Color){ 165, 215, 255, 255 }, inst->pulse01 * 0.24f * vis), 40);
            }
            MagZone_DrawGroundRing(inst->center, r - 0.02f, r + 0.02f,
                                   VC_Premultiply((Color){ 165, 215, 255, 255 }, aPerimeter * 0.65f), 48);

            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            rlDrawRenderBatchActive();
            VFXRender_EndDraw(&scope);
        }
    }
}

#endif /* VC_METAL_MAGNETIC_ZONE_INL */
