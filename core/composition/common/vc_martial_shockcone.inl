// ── Shared Martial Primitive: Directional 3D Pressure / Qi Shock-Cone ──────────
// Swept paraboloid Mach/Qi cone for close-combat punches, double-palm blasts,
// flying kicks, and shoulder rams. Dual-pass BODY + EMISSION, zero heap alloc.

#define VC_SHOCKCONE_MAX 12

typedef struct {
    bool active;
    Vector3 origin;
    Vector3 dir;
    VC_MaterialId mat;
    float length;
    float maxRadius;
    float elapsed;
    float duration;
} VC_MartialShockConeInst;

static VC_MartialShockConeInst s_shockCones[VC_SHOCKCONE_MAX] = {0};

int VFX_MartialShockCone_Spawn(Vector3 origin, Vector3 dir, VC_MaterialId mat,
                               float length, float maxRadius, float duration)
{
    int slot = -1;
    for (int i = 0; i < VC_SHOCKCONE_MAX; i++) {
        if (!s_shockCones[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    Vector3 d = (Vector3LengthSqr(dir) > 1e-5f) ? Vector3Normalize(dir) : (Vector3){ 0.0f, 0.0f, 1.0f };
    VC_MartialShockConeInst *inst = &s_shockCones[slot];
    inst->active    = true;
    inst->origin    = origin;
    inst->dir       = d;
    inst->mat       = mat;
    inst->length    = (length > 0.2f) ? length : 2.2f;
    inst->maxRadius = (maxRadius > 0.1f) ? maxRadius : 0.85f;
    inst->elapsed   = 0.0f;
    inst->duration  = (duration > 0.06f) ? duration : 0.28f;
    ScreenDistort_Add(Vector3Add(origin, Vector3Scale(d, inst->length * 0.45f)),
                      inst->maxRadius * 1.4f, 0.035f, inst->duration * 0.8f, 1.8f);
    return slot + 1;
}

static void VC_MartialShockCone_Update(float dt)
{
    for (int i = 0; i < VC_SHOCKCONE_MAX; i++) {
        if (!s_shockCones[i].active) continue;
        s_shockCones[i].elapsed += dt;
        if (s_shockCones[i].elapsed >= s_shockCones[i].duration) {
            s_shockCones[i].active = false;
        }
    }
}

static void VC_MartialShockCone_Draw3D(Camera3D cam)
{
    (void)cam;
    const int SLICES = 18;
    const int RINGS  = 5;
    for (int i = 0; i < VC_SHOCKCONE_MAX; i++) {
        const VC_MartialShockConeInst *inst = &s_shockCones[i];
        if (!inst->active) continue;
        float t01 = Clamp(inst->elapsed / inst->duration, 0.0f, 1.0f);
        float ease = 1.0f - powf(1.0f - t01, 2.2f);
        float fade = sinf(t01 * PI);
        fade = powf(fmaxf(0.0f, fade), 0.75f);
        if (fade <= 0.01f) continue;

        const VFX_ElementMaterial *m = VFX_Material(inst->mat);
        Vector3 d = inst->dir;
        Vector3 ref = (fabsf(d.y) < 0.95f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
        Vector3 right = Vector3Normalize(Vector3CrossProduct(ref, d));
        Vector3 up    = Vector3CrossProduct(d, right);

        float curLen = inst->length * (0.35f + 0.65f * ease);
        float curRad = inst->maxRadius * (0.45f + 0.55f * ease);
        Vector3 apex = Vector3Add(inst->origin, Vector3Scale(d, curLen * 0.22f * ease));

        for (int pass = 0; pass < 2; pass++) {
            VFXRenderScope scope = VFXRender_BeginDraw(
                pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
                pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_PREMULTIPLIED,
                false);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlSetTexture(0);
            rlBegin(RL_TRIANGLES);

            Color baseC = (pass == 0)
                ? (Color){ (unsigned char)(m->body.r * 0.28f), (unsigned char)(m->body.g * 0.28f), (unsigned char)(m->body.b * 0.28f), 255 }
                : VC_Whiten(m->glow, 0.45f);
            float rMul = (pass == 0) ? 1.04f : 0.96f;

            for (int r = 0; r < RINGS; r++) {
                float u0 = (float)r / (float)RINGS;
                float u1 = (float)(r + 1) / (float)RINGS;
                /* Paraboloid flare: tight at tip (u=0), wide trailing skirt (u=1) */
                float rad0 = curRad * powf(u0, 0.65f) * rMul;
                float rad1 = curRad * powf(u1, 0.65f) * rMul;
                float z0 = curLen * (1.0f - u0);
                float z1 = curLen * (1.0f - u1);
                float a0 = fade * ((pass == 0) ? (0.62f * (1.0f - u0 * 0.75f)) : (0.85f * powf(1.0f - u0, 1.8f)));
                float a1 = fade * ((pass == 0) ? (0.62f * (1.0f - u1 * 0.75f)) : (0.85f * powf(1.0f - u1, 1.8f)));
                Color c0 = (pass == 0) ? ColorAlpha(baseC, Clamp(a0, 0.0f, 1.0f)) : VC_Premultiply(baseC, Clamp(a0, 0.0f, 1.0f));
                Color c1 = (pass == 0) ? ColorAlpha(baseC, Clamp(a1, 0.0f, 1.0f)) : VC_Premultiply(baseC, Clamp(a1, 0.0f, 1.0f));

                for (int s = 0; s < SLICES; s++) {
                    float ang0 = (float)s / (float)SLICES * 2.0f * PI + t01 * 1.4f;
                    float ang1 = (float)(s + 1) / (float)SLICES * 2.0f * PI + t01 * 1.4f;
                    Vector3 off0 = Vector3Add(Vector3Scale(right, cosf(ang0)), Vector3Scale(up, sinf(ang0)));
                    Vector3 off1 = Vector3Add(Vector3Scale(right, cosf(ang1)), Vector3Scale(up, sinf(ang1)));
                    Vector3 p00 = Vector3Add(Vector3Add(apex, Vector3Scale(d, z0)), Vector3Scale(off0, rad0));
                    Vector3 p01 = Vector3Add(Vector3Add(apex, Vector3Scale(d, z0)), Vector3Scale(off1, rad0));
                    Vector3 p10 = Vector3Add(Vector3Add(apex, Vector3Scale(d, z1)), Vector3Scale(off0, rad1));
                    Vector3 p11 = Vector3Add(Vector3Add(apex, Vector3Scale(d, z1)), Vector3Scale(off1, rad1));

                    rlColor4ub(c0.r, c0.g, c0.b, c0.a); rlVertex3f(p00.x, p00.y, p00.z);
                    rlColor4ub(c0.r, c0.g, c0.b, c0.a); rlVertex3f(p01.x, p01.y, p01.z);
                    rlColor4ub(c1.r, c1.g, c1.b, c1.a); rlVertex3f(p11.x, p11.y, p11.z);

                    rlColor4ub(c0.r, c0.g, c0.b, c0.a); rlVertex3f(p00.x, p00.y, p00.z);
                    rlColor4ub(c1.r, c1.g, c1.b, c1.a); rlVertex3f(p11.x, p11.y, p11.z);
                    rlColor4ub(c1.r, c1.g, c1.b, c1.a); rlVertex3f(p10.x, p10.y, p10.z);
                }
            }
            rlEnd();
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            VFXRender_EndDraw(&scope);
        }
    }
}
