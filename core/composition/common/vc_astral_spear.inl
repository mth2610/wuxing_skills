// ASTRAL SPEAR — an independent, directional projectile composition.
//
// This is not an orb with a tail. Its readable mass is a long faceted dart,
// the light lives only in narrow seams and a split wake, and two incomplete
// halos compress around the shoulder. The three silhouettes still separate at
// gameplay distance: dark-edged spearhead, saturated taper, white-hot filament.
//
// The caller owns one Matrix and only moves its origin. Heading is derived from
// that motion, so steering does not require a second direction channel. The
// composition owns its fixed history and lifecycle; no allocation, FlowShield,
// TrailSystem, shader, or texture asset is involved.

#define ASTRAL_SPEAR_MAX 6
#define ASTRAL_SPEAR_HISTORY 30
#define ASTRAL_SPEAR_SAMPLE_DT (1.0f / 60.0f)
#define ASTRAL_SPEAR_HALF_LENGTH 1.85f
#define ASTRAL_SPEAR_WAKE_WIDTH 0.78f
#define ASTRAL_SPEAR_CORE_WIDTH 0.16f
#define ASTRAL_SPEAR_HALO_SEGMENTS 24
#define ASTRAL_SPEAR_SIDES 8

typedef struct {
    bool active;
    bool stopping;
    bool hasPrev;
    const Matrix *xf;
    VC_MaterialId mat;
    Vector3 pos;
    Vector3 prevPos;
    Vector3 heading;
    Vector3 history[ASTRAL_SPEAR_HISTORY]; // oldest -> newest
    int historyCount;
    float sampleClock;
    float radius;
    float speed;
    float level;
    float target;
    float elapsed;
    float phase;
} VC_AstralSpear;

static VC_AstralSpear s_astralSpears[ASTRAL_SPEAR_MAX];

static float AstralSpear_Clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float AstralSpear_Smooth01(float x)
{
    x = AstralSpear_Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

static Color AstralSpear_Shade(Color c, float gain, unsigned char alpha)
{
    float r = (float)c.r * gain;
    float g = (float)c.g * gain;
    float b = (float)c.b * gain;
    c.r = (unsigned char)Clamp(r, 0.0f, 255.0f);
    c.g = (unsigned char)Clamp(g, 0.0f, 255.0f);
    c.b = (unsigned char)Clamp(b, 0.0f, 255.0f);
    c.a = alpha;
    return c;
}

static void AstralSpear_Frame(Vector3 heading, Vector3 *outRight, Vector3 *outUp)
{
    Vector3 ref = (fabsf(heading.y) < 0.92f)
                      ? (Vector3){0.0f, 1.0f, 0.0f}
                      : (Vector3){1.0f, 0.0f, 0.0f};
    *outRight = Vector3Normalize(Vector3CrossProduct(heading, ref));
    *outUp = Vector3Normalize(Vector3CrossProduct(*outRight, heading));
}

static Vector3 AstralSpear_RingPoint(Vector3 center, Vector3 right, Vector3 up,
                                     float radius, float angle)
{
    return VC_MotionOrbitAxis(center, right, up, radius, angle);
}

static void AstralSpear_PushHistory(VC_AstralSpear *s, Vector3 p)
{
    if (s->historyCount < ASTRAL_SPEAR_HISTORY)
    {
        s->history[s->historyCount++] = p;
        return;
    }
    for (int i = 1; i < ASTRAL_SPEAR_HISTORY; ++i)
        s->history[i - 1] = s->history[i];
    s->history[ASTRAL_SPEAR_HISTORY - 1] = p;
}

int VFX_ComposeAstralSpear(const Matrix *followTransform, VC_MaterialId mat,
                           float radius)
{
    if (followTransform == NULL) return -1;
    int slot = -1;
    for (int i = 0; i < ASTRAL_SPEAR_MAX; ++i)
        if (!s_astralSpears[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    VC_AstralSpear *s = &s_astralSpears[slot];
    *s = (VC_AstralSpear){0};
    s->active = true;
    s->xf = followTransform;
    s->mat = mat;
    s->radius = Clamp(radius, 0.07f, 0.15f);
    s->pos = (Vector3){followTransform->m12, followTransform->m13,
                       followTransform->m14};
    s->prevPos = s->pos;
    s->heading = (Vector3){1.0f, 0.0f, 0.0f};
    s->level = 0.08f;
    s->target = 1.0f;
    s->phase = (float)slot * 2.39996f;
    AstralSpear_PushHistory(s, s->pos);
    return slot;
}

void VFX_AstralSpear_SetIntensity(int handle, float intensity01)
{
    if (handle < 0 || handle >= ASTRAL_SPEAR_MAX ||
        !s_astralSpears[handle].active) return;
    s_astralSpears[handle].target = AstralSpear_Clamp01(intensity01);
}

void VFX_AstralSpear_Stop(int handle)
{
    if (handle < 0 || handle >= ASTRAL_SPEAR_MAX ||
        !s_astralSpears[handle].active) return;
    s_astralSpears[handle].stopping = true;
    s_astralSpears[handle].target = 0.0f;
}

void VFX_KillAstralSpear(int handle)
{
    if (handle < 0 || handle >= ASTRAL_SPEAR_MAX) return;
    s_astralSpears[handle].active = false;
}

static void VC_AstralSpear_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < ASTRAL_SPEAR_MAX; ++i)
    {
        VC_AstralSpear *s = &s_astralSpears[i];
        if (!s->active) continue;

        s->elapsed += dt;
        Vector3 p = {s->xf->m12, s->xf->m13, s->xf->m14};
        Vector3 delta = Vector3Subtract(p, s->prevPos);
        float dist = Vector3Length(delta);
        if (s->hasPrev && dist > 1.0e-4f)
        {
            float response = 1.0f - expf(-dt * 13.0f);
            Vector3 measured = Vector3Scale(delta, 1.0f / dist);
            Vector3 filtered = Vector3Lerp(s->heading, measured, response);
            if (Vector3LengthSqr(filtered) > 1.0e-8f)
                s->heading = Vector3Normalize(filtered);
            s->speed += ((dist / dt) - s->speed) * response;
        }
        s->hasPrev = true;

        // Fixed-rate history with interpolation. A hitch may add several
        // samples, but never more than the ring can consume in one frame.
        s->sampleClock += dt;
        int samples = (int)(s->sampleClock / ASTRAL_SPEAR_SAMPLE_DT);
        if (samples > 6) samples = 6;
        if (samples > 0 && dist > 1.0e-4f)
        {
            for (int k = 1; k <= samples; ++k)
            {
                float f = (float)k / (float)samples;
                AstralSpear_PushHistory(s, Vector3Lerp(s->prevPos, p, f));
            }
            s->sampleClock -= (float)samples * ASTRAL_SPEAR_SAMPLE_DT;
        }
        if (s->sampleClock > ASTRAL_SPEAR_SAMPLE_DT * 6.0f)
            s->sampleClock = ASTRAL_SPEAR_SAMPLE_DT * 6.0f;

        s->prevPos = p;
        s->pos = p;
        s->level += (s->target - s->level) * (1.0f - expf(-dt * 11.0f));
        if (s->stopping && s->level < 0.004f)
        {
            s->active = false;
            continue;
        }

        if (s->level > 0.02f)
        {
            const VFX_ElementMaterial *m = VFX_Material(s->mat);
            float speedLight = AstralSpear_Clamp01(s->speed / 2.5f);
            VFXLight_Spawn(s->pos, m->glow,
                           s->radius * (2.5f + speedLight) * s->level,
                           0.08f, VFX_PRIORITY_LOW);
        }
    }
}

static void AstralSpear_Vertex(Vector3 p)
{
    rlVertex3f(p.x, p.y, p.z);
}

static void AstralSpear_DrawFacet(Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                                  Vector3 normal, Color base, float alpha)
{
    const Vector3 key = {0.424264f, 0.678823f, 0.424264f};
    float ndl = Vector3DotProduct(normal, key);
    if (ndl < 0.0f) ndl = 0.0f;
    float rim = 1.0f - fabsf(Vector3DotProduct(normal, key));
    float shade = 0.34f + 0.52f * ndl + 0.10f * rim * rim;
    Color c0 = AstralSpear_Shade(base, shade,
                                 (unsigned char)(255.0f * alpha));
    rlColor4ub(c0.r, c0.g, c0.b, c0.a);
    AstralSpear_Vertex(a); AstralSpear_Vertex(b); AstralSpear_Vertex(c);
    AstralSpear_Vertex(a); AstralSpear_Vertex(c); AstralSpear_Vertex(d);
}

static void AstralSpear_DrawHeadBody(const VC_AstralSpear *s,
                                     const VFX_ElementMaterial *m)
{
    Vector3 right, up;
    AstralSpear_Frame(s->heading, &right, &up);
    float r = s->radius * VC_Breathe(s->elapsed + s->phase, 4.1f, 0.025f);
    Vector3 rear = Vector3Add(s->pos, Vector3Scale(s->heading,
                                                   -ASTRAL_SPEAR_HALF_LENGTH * r));
    Vector3 rearRing = Vector3Add(s->pos, Vector3Scale(s->heading, -0.42f * r));
    Vector3 frontRing = Vector3Add(s->pos, Vector3Scale(s->heading, 0.36f * r));
    Vector3 nose = Vector3Add(s->pos, Vector3Scale(s->heading,
                                                   ASTRAL_SPEAR_HALF_LENGTH * r));
    Color body = VC_MixColor(m->body, m->soft, 0.10f);
    float alpha = 0.88f * s->level;

    Vector3 backPts[ASTRAL_SPEAR_SIDES];
    Vector3 frontPts[ASTRAL_SPEAR_SIDES];
    for (int k = 0; k < ASTRAL_SPEAR_SIDES; ++k)
    {
        float a = (float)k * 2.0f * PI / (float)ASTRAL_SPEAR_SIDES + s->phase;
        backPts[k] = AstralSpear_RingPoint(rearRing, right, up, r * 0.58f, a);
        frontPts[k] = AstralSpear_RingPoint(frontRing, right, up, r * 0.82f, a);
    }

    VFXRenderScope scope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, true);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int k = 0; k < ASTRAL_SPEAR_SIDES; ++k)
    {
        int n = (k + 1) % ASTRAL_SPEAR_SIDES;
        Vector3 mid = Vector3Scale(Vector3Add(frontPts[k], frontPts[n]), 0.5f);
        Vector3 normal = Vector3Normalize(Vector3Subtract(mid, frontRing));
        AstralSpear_DrawFacet(rear, backPts[n], backPts[k], rear,
                              Vector3Negate(normal), body, alpha);
        AstralSpear_DrawFacet(backPts[k], backPts[n], frontPts[n], frontPts[k],
                              normal, body, alpha);
        AstralSpear_DrawFacet(frontPts[k], frontPts[n], nose, nose,
                              normal, body, alpha);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    VFXRender_EndDraw(&scope);
}

static void AstralSpear_DrawStrip(Vector3 a, Vector3 b, Vector3 radial,
                                  float halfWidth, Color color)
{
    Vector3 side = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), radial));
    side = Vector3Scale(side, halfWidth);
    Vector3 a0 = Vector3Subtract(a, side), a1 = Vector3Add(a, side);
    Vector3 b0 = Vector3Subtract(b, side), b1 = Vector3Add(b, side);
    rlColor4ub(color.r, color.g, color.b, color.a);
    AstralSpear_Vertex(a0); AstralSpear_Vertex(a1); AstralSpear_Vertex(b1);
    AstralSpear_Vertex(a0); AstralSpear_Vertex(b1); AstralSpear_Vertex(b0);
}

static void AstralSpear_DrawHalo(Vector3 center, Vector3 right, Vector3 up,
                                 float radius, float spin, int offset, Color color)
{
    for (int k = 0; k < ASTRAL_SPEAR_HALO_SEGMENTS; ++k)
    {
        // Unequal 2/7 cuts keep it a broken magical glyph, not a machine ring.
        if (((k + offset) % 7) < 2) continue;
        float a0 = spin + (float)k * 2.0f * PI / (float)ASTRAL_SPEAR_HALO_SEGMENTS;
        float a1 = spin + (float)(k + 1) * 2.0f * PI / (float)ASTRAL_SPEAR_HALO_SEGMENTS;
        Vector3 p0 = AstralSpear_RingPoint(center, right, up, radius, a0);
        Vector3 p1 = AstralSpear_RingPoint(center, right, up, radius, a1);
        Vector3 q0 = AstralSpear_RingPoint(center, right, up, radius * 0.91f, a0);
        Vector3 q1 = AstralSpear_RingPoint(center, right, up, radius * 0.91f, a1);
        rlColor4ub(color.r, color.g, color.b, color.a);
        AstralSpear_Vertex(q0); AstralSpear_Vertex(p0); AstralSpear_Vertex(p1);
        AstralSpear_Vertex(q0); AstralSpear_Vertex(p1); AstralSpear_Vertex(q1);
    }
}

static void AstralSpear_DrawHeadEmission(const VC_AstralSpear *s,
                                         const VFX_ElementMaterial *m)
{
    Vector3 right, up;
    AstralSpear_Frame(s->heading, &right, &up);
    float r = s->radius;
    Vector3 rear = Vector3Add(s->pos, Vector3Scale(s->heading, -1.42f * r));
    Vector3 nose = Vector3Add(s->pos, Vector3Scale(s->heading, 1.76f * r));
    Color seam = VC_WithAlpha(VC_Whiten(m->glow, 0.58f),
                              (unsigned char)(190.0f * s->level));
    Color halo = VC_WithAlpha(m->glow, (unsigned char)(120.0f * s->level));

    VFXRenderScope scope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);

    // Three unequal seams: enough to turn as the dart rotates without wrapping
    // the whole body in uniform emission. Drawn twice so the compact source can
    // cross the HDR bloom threshold while the broad body stays sub-threshold.
    static const float seamPhase[3] = {0.15f, 2.20f, 4.75f};
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 3; ++k)
        {
            Vector3 radial = Vector3Add(
                Vector3Scale(right, cosf(seamPhase[k] + s->phase)),
                Vector3Scale(up, sinf(seamPhase[k] + s->phase)));
            Vector3 a = Vector3Add(rear, Vector3Scale(radial, r * 0.22f));
            Vector3 b = Vector3Add(nose, Vector3Scale(radial, r * 0.06f));
            AstralSpear_DrawStrip(a, b, radial, r * 0.028f, seam);
        }

    float pulse = VC_Breathe(s->elapsed + s->phase, 3.7f, 0.08f);
    Vector3 haloA = Vector3Add(s->pos, Vector3Scale(s->heading, -0.18f * r));
    Vector3 haloB = Vector3Add(s->pos, Vector3Scale(s->heading, -0.82f * r));
    AstralSpear_DrawHalo(haloA, right, up, r * 1.20f * pulse,
                         s->elapsed * 2.7f + s->phase, 0, halo);
    AstralSpear_DrawHalo(haloB, right, up, r * 0.92f,
                         -s->elapsed * 3.4f + s->phase * 0.7f, 3, halo);

    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    VFXRender_EndDraw(&scope);
}

static void AstralSpear_BuildWake(const VC_AstralSpear *s, RibbonPoint *body,
                                  RibbonPoint *core, int *outCount)
{
    const VFX_ElementMaterial *m = VFX_Material(s->mat);
    int count = s->historyCount;
    if (count > ASTRAL_SPEAR_HISTORY) count = ASTRAL_SPEAR_HISTORY;
    for (int i = 0; i < count; ++i)
    {
        float t = (count > 1) ? (float)i / (float)(count - 1) : 1.0f;
        float shoulder = AstralSpear_Smooth01(t);
        float fade = t * s->level;
        Vector3 p = s->history[s->historyCount - count + i];

        // A single aerodynamic wake, not a braid. Its slow lateral pressure
        // wave is zero at the head, strongest mid-tail, and uses the shared
        // arbitrary-axis orbit primitive so it follows vertical steering too.
        Vector3 right, up;
        AstralSpear_Frame(s->heading, &right, &up);
        float envelope = sinf(t * PI) * (1.0f - t);
        float angle = s->phase + s->elapsed * 2.1f + (float)i * 0.47f;
        p = VC_MotionOrbitAxis(p, right, up,
                               s->radius * 0.24f * envelope, angle);

        body[i].position = p;
        body[i].halfWidth = s->radius * ASTRAL_SPEAR_WAKE_WIDTH * shoulder;
        body[i].tint = VC_WithAlpha(VC_MixColor(m->body, m->glow, 0.28f),
                                    (unsigned char)(142.0f * fade));
        core[i].position = p;
        core[i].halfWidth = s->radius * ASTRAL_SPEAR_CORE_WIDTH * shoulder;
        core[i].tint = VC_WithAlpha(VC_Whiten(m->glow, 0.72f),
                                    (unsigned char)(205.0f * fade));
    }
    if (count >= 2)
    {
        Ribbon_ComputeArcLengthUV(body, count);
        Ribbon_ComputeArcLengthUV(core, count);
    }
    *outCount = count;
}

static void AstralSpear_DrawWake(const VC_AstralSpear *s, Camera3D cam)
{
    if (s->historyCount < 2 || s->level <= 0.02f) return;
    static RibbonPoint body[ASTRAL_SPEAR_HISTORY];
    static RibbonPoint core[ASTRAL_SPEAR_HISTORY];
    int count = 0;
    AstralSpear_BuildWake(s, body, core, &count);
    if (count < 2) return;

    Texture2D white = {0};
    white.id = rlGetTextureIdDefault();
    white.width = white.height = 1;
    white.mipmaps = 1;
    white.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    VFXRibbonDrawConfig bodyCfg = {
        .mode = RIBBON_CAMERA_FACING,
        .fixedNormal = {0.0f, 1.0f, 0.0f},
        .pass = VFX_RENDER_PASS_BODY,
        .appearance = VFX_APPEARANCE_MAGIC,
        .legacyAppearance = {0},
        .depthWrite = false,
    };
    DrawRibbonStripAppearanceEx(body, count, white, cam, &bodyCfg);

    VFXRibbonDrawConfig coreCfg = bodyCfg;
    coreCfg.pass = VFX_RENDER_PASS_EMISSION;
    coreCfg.appearance = VFX_APPEARANCE_GLOW;
    DrawRibbonStripAppearanceEx(core, count, white, cam, &coreCfg);
    // A second compact submission creates real HDR energy without inflating
    // the colored wake or turning the entire projectile white.
    DrawRibbonStripAppearanceEx(core, count, white, cam, &coreCfg);
}

static void VC_AstralSpear_Draw3D(Camera3D cam)
{
    for (int i = 0; i < ASTRAL_SPEAR_MAX; ++i)
    {
        const VC_AstralSpear *s = &s_astralSpears[i];
        if (!s->active || s->level <= 0.02f) continue;
        const VFX_ElementMaterial *m = VFX_Material(s->mat);
        AstralSpear_DrawWake(s, cam);
        AstralSpear_DrawHeadBody(s, m);
        AstralSpear_DrawHeadEmission(s, m);
    }
}
