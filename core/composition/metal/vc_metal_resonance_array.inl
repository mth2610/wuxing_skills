// vc_metal_resonance_array.inl — Tier-3 Acoustic Resonance Sword Array (Chấn Âm Kiếm Trận)
// Included once via metal/metal.inl -> visual_composer.c (after vc_metal_blades.inl)
//
// 3-Phase Kinematics (C9 + C10 compliant):
//   Phase 1 (0.00s -> telegraphSecs >= 0.65s): 6 physical 3D Jian blades pin into ground,
//     emitting concentric Chladni acoustic standing-wave rings & metallic ground cracks (y <= 0.25m).
//   Phase 2 (telegraphSecs -> +eruptSecs <= 0.35s): Vertical sword-qi spires erupt upward
//     around each pinned blade + central resonance shockwave.
//   Phase 3 (+0.25s): Magnetic sword recall lift & dissolve.

#ifndef VC_METAL_RESONANCE_ARRAY_INL
#define VC_METAL_RESONANCE_ARRAY_INL

#ifndef PI
#define PI 3.1415926535f
#endif

#define VC_RES_ARRAY_MAX     6
#define VC_RES_BLADE_COUNT   6

typedef struct {
    bool    active;
    int     id;
    Vector3 casterPos;
    Vector3 center;
    float   radius;
    float   telegraphSecs;
    float   eruptSecs;
    float   elapsed;
    bool    erupted;
    Vector3 bladePts[VC_RES_BLADE_COUNT];
    float   bladeAngles[VC_RES_BLADE_COUNT];
} VC_MetalResArrayInst;

static VC_MetalResArrayInst s_resArrays[VC_RES_ARRAY_MAX];
static int                  s_resNextId = 1;

static void ResArray_DrawGroundChladni(Vector3 center, float radius, float t01, float alpha01)
{
    if (alpha01 <= 0.01f) return;
    const int SEGS = 48;
    float rRings[3] = { radius * 0.36f, radius * 0.68f, radius * 0.98f };

    /* Pass 1: Dark metallic ground groove (Body) */
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        rlSetTexture(0);

        for (int k = 0; k < 3; k++) {
            float vib = 1.0f + 0.035f * sinf(t01 * 42.0f + (float)k * 2.1f);
            float rIn  = fmaxf(0.05f, rRings[k] * vib - 0.06f);
            float rOut = rRings[k] * vib + 0.06f;
            Color c = ColorAlpha((Color){ 32, 38, 48, 255 }, Clamp(alpha01 * 0.72f, 0.0f, 1.0f));
            rlBegin(RL_TRIANGLES);
            rlColor4ub(c.r, c.g, c.b, c.a);
            for (int s = 0; s < SEGS; s++) {
                float a0 = (float)s / (float)SEGS * 2.0f * PI;
                float a1 = (float)(s + 1) / (float)SEGS * 2.0f * PI;
                float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
                float x0i = center.x + c0 * rIn,  z0i = center.z + s0 * rIn;
                float x1i = center.x + c1 * rIn,  z1i = center.z + s1 * rIn;
                float x0o = center.x + c0 * rOut, z0o = center.z + s0 * rOut;
                float x1o = center.x + c1 * rOut, z1o = center.z + s1 * rOut;
                float y0i = VFX_GroundHeightFromMap(x0i, z0i, NULL) + 0.04f;
                float y1i = VFX_GroundHeightFromMap(x1i, z1i, NULL) + 0.04f;
                float y0o = VFX_GroundHeightFromMap(x0o, z0o, NULL) + 0.04f;
                float y1o = VFX_GroundHeightFromMap(x1o, z1o, NULL) + 0.04f;
                rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1i, y1i, z1i); rlVertex3f(x1o, y1o, z1o);
                rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1o, y1o, z1o); rlVertex3f(x0o, y0o, z0o);
            }
            rlEnd();
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }

    /* Pass 2: Acoustic platinum shimmer line (Emission) */
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        rlSetTexture(0);

        float pulse = 0.45f + 0.55f * sinf(t01 * 28.0f);
        Color eCol = VC_Premultiply((Color){ 185, 225, 255, 255 }, Clamp(alpha01 * (0.55f + 0.45f * pulse), 0.0f, 1.0f));
        for (int k = 0; k < 3; k++) {
            float vib = 1.0f + 0.035f * sinf(t01 * 42.0f + (float)k * 2.1f);
            float rIn  = fmaxf(0.05f, rRings[k] * vib - 0.024f);
            float rOut = rRings[k] * vib + 0.024f;
            rlBegin(RL_TRIANGLES);
            rlColor4ub(eCol.r, eCol.g, eCol.b, eCol.a);
            for (int s = 0; s < SEGS; s++) {
                float a0 = (float)s / (float)SEGS * 2.0f * PI;
                float a1 = (float)(s + 1) / (float)SEGS * 2.0f * PI;
                float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
                float x0i = center.x + c0 * rIn,  z0i = center.z + s0 * rIn;
                float x1i = center.x + c1 * rIn,  z1i = center.z + s1 * rIn;
                float x0o = center.x + c0 * rOut, z0o = center.z + s0 * rOut;
                float x1o = center.x + c1 * rOut, z1o = center.z + s1 * rOut;
                float y0i = VFX_GroundHeightFromMap(x0i, z0i, NULL) + 0.048f;
                float y1i = VFX_GroundHeightFromMap(x1i, z1i, NULL) + 0.048f;
                float y0o = VFX_GroundHeightFromMap(x0o, z0o, NULL) + 0.048f;
                float y1o = VFX_GroundHeightFromMap(x1o, z1o, NULL) + 0.048f;
                rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1i, y1i, z1i); rlVertex3f(x1o, y1o, z1o);
                rlVertex3f(x0i, y0i, z0i); rlVertex3f(x1o, y1o, z1o); rlVertex3f(x0o, y0o, z0o);
            }
            rlEnd();
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

static void ResArray_DrawVerticalSpire(Vector3 basePos, float height, float halfW, float alpha01, Camera3D cam)
{
    if (height <= 0.05f || alpha01 <= 0.01f) return;
    static RibbonPoint bodyPts[8];
    static RibbonPoint edgePts[8];
    Color steelDark = (Color){ 36, 44, 58, 255 };
    Color silverHot = (Color){ 230, 244, 255, 255 };

    for (int i = 0; i < 8; i++) {
        float u = (float)i / 7.0f;
        Vector3 p = { basePos.x, basePos.y + height * u, basePos.z };
        float taper = powf(1.0f - u, 0.7f) * ((u < 0.15f) ? (u / 0.15f) : 1.0f);
        bodyPts[i].position  = p;
        bodyPts[i].halfWidth = halfW * taper;
        bodyPts[i].tint      = ColorAlpha(steelDark, Clamp(alpha01 * 0.85f, 0.0f, 1.0f));
        bodyPts[i].v         = u;

        edgePts[i].position  = p;
        edgePts[i].halfWidth = halfW * 0.34f * taper;
        edgePts[i].tint      = VC_Premultiply(silverHot, Clamp(alpha01 * 0.95f, 0.0f, 1.0f));
        edgePts[i].v         = u;
    }

    Texture2D tex = MetalSlash_GetMask();
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        DrawRibbonStrip(bodyPts, 8, tex, cam);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
    {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();
        DrawRibbonStrip(edgePts, 8, tex, cam);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

int VFX_MetalResonanceArray_Spawn(Vector3 casterPos, Vector3 targetPos, float radius,
                                  float telegraphSecs, float eruptSecs)
{
    MetalBlade_InitMesh();
    int slot = -1;
    for (int i = 0; i < VC_RES_ARRAY_MAX; i++) {
        if (!s_resArrays[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    VC_MetalResArrayInst *inst = &s_resArrays[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active        = true;
    inst->id            = s_resNextId++;
    inst->casterPos     = casterPos;
    inst->center        = targetPos;
    inst->center.y      = VFX_GroundHeightFromMap(targetPos.x, targetPos.z, NULL);
    inst->radius        = (radius > 0.5f) ? radius : 2.8f;
    inst->telegraphSecs = (telegraphSecs >= 0.60f) ? telegraphSecs : 0.65f;
    inst->eruptSecs     = (eruptSecs > 0.05f) ? eruptSecs : 0.35f;

    for (int k = 0; k < VC_RES_BLADE_COUNT; k++) {
        float ang = (float)k * (2.0f * PI / (float)VC_RES_BLADE_COUNT) + 0.25f;
        float r = inst->radius * ((k & 1) ? 0.84f : 0.68f);
        float px = inst->center.x + cosf(ang) * r;
        float pz = inst->center.z + sinf(ang) * r;
        float py = VFX_GroundHeightFromMap(px, pz, NULL);
        inst->bladePts[k]    = (Vector3){ px, py, pz };
        inst->bladeAngles[k] = ang;
    }

    /* Ground metallic cracks for telegraph */
    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration    = inst->telegraphSecs;
    ccfg.holdDuration      = inst->eruptSecs;
    ccfg.fadeDuration      = 0.35f;
    ccfg.veinGlowIntensity = 0.26f;
    ccfg.emitDebris        = false;
    ccfg.emitDust          = false;
    VFX_ComposeEarthCrackRadial(inst->center, inst->radius, &ccfg);
    return inst->id;
}

void VFX_MetalResonanceArray_Kill(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_RES_ARRAY_MAX; i++) {
        if (s_resArrays[i].active && s_resArrays[i].id == handle) {
            s_resArrays[i].active = false;
            return;
        }
    }
}

void VC_MetalResonanceArray_Update(float dt)
{
    for (int i = 0; i < VC_RES_ARRAY_MAX; i++) {
        VC_MetalResArrayInst *inst = &s_resArrays[i];
        if (!inst->active) continue;
        inst->elapsed += dt;
        if (!inst->erupted && inst->elapsed >= inst->telegraphSecs) {
            inst->erupted = true;
            VFX_ComposeContactSparkMode(inst->center, VC_MAT_METAL, 1.05f, 0.95f, CONTACT_SPARK_CENTRIFUGAL);
            Wind_SpawnRadialBlast(inst->center, inst->radius * 1.25f, 8.5f, 0.28f);
        }
        if (inst->elapsed >= inst->telegraphSecs + inst->eruptSecs + 0.25f) {
            inst->active = false;
        }
    }
}

void VC_MetalResonanceArray_Draw3D(Camera3D cam)
{
    MetalBlade_InitMesh();
    for (int i = 0; i < VC_RES_ARRAY_MAX; i++) {
        const VC_MetalResArrayInst *inst = &s_resArrays[i];
        if (!inst->active) continue;

        float total = inst->telegraphSecs + inst->eruptSecs + 0.25f;
        float tTele = Clamp(inst->elapsed / inst->telegraphSecs, 0.0f, 1.0f);
        float alphaGround = (inst->elapsed < inst->telegraphSecs)
            ? (0.35f + 0.65f * tTele)
            : Clamp(1.0f - (inst->elapsed - inst->telegraphSecs) / (inst->eruptSecs + 0.25f), 0.0f, 1.0f);
        ResArray_DrawGroundChladni(inst->center, inst->radius, tTele, alphaGround);

        /* Draw 6 pinned 3D auxiliary Jian blades */
        if (s_bladeShader.id != 0 && s_subSteel.mesh.vertexCount > 0) {
            s_bladeDrawMat.shader = s_bladeShader;
            float now = TimeFX_Elapsed();
            if (s_bladeLocTime >= 0) SetShaderValue(s_bladeShader, s_bladeLocTime, &now, SHADER_UNIFORM_FLOAT);

            float bladeBase[4] = { 0.48f, 0.54f, 0.62f, 1.0f };
            float bladeRim[4]  = { 0.78f, 0.90f, 1.00f, 1.0f };
            float goldBase[4]  = { 0.85f, 0.65f, 0.22f, 1.0f };
            float goldRim[4]   = { 1.00f, 0.88f, 0.45f, 1.0f };

            float recallT = Clamp((inst->elapsed - (inst->telegraphSecs + inst->eruptSecs)) / 0.25f, 0.0f, 1.0f);
            for (int k = 0; k < VC_RES_BLADE_COUNT; k++) {
                float dropT = Clamp(inst->elapsed / 0.14f, 0.0f, 1.0f);
                Vector3 pos = inst->bladePts[k];
                pos.y += (1.0f - dropT) * 1.8f + 0.58f;
                if (recallT > 0.0f) {
                    pos = Vector3Lerp(pos, Vector3Add(inst->casterPos, (Vector3){ 0.0f, 1.2f, 0.0f }), recallT * recallT);
                }
                float vibX = (inst->elapsed < inst->telegraphSecs)
                    ? sinf(inst->elapsed * 75.0f + (float)k) * 0.025f * tTele : 0.0f;
                pos.x += vibX;
                Vector3 bladeFwd = { cosf(inst->bladeAngles[k]) * 0.14f, -0.98f, sinf(inst->bladeAngles[k]) * 0.14f };
                float bScale = 0.85f * (1.0f - recallT * 0.75f);
                Matrix xf = MetalBlade_BuildTransform(pos, bladeFwd, (float)k * 35.0f, bScale);

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

        /* Phase 2: Vertical sword-qi spires erupting upward (<= 0.35s) */
        if (inst->elapsed >= inst->telegraphSecs && inst->elapsed < inst->telegraphSecs + inst->eruptSecs) {
            float e01 = Clamp((inst->elapsed - inst->telegraphSecs) / inst->eruptSecs, 0.0f, 1.0f);
            float hEnv = (e01 < 0.22f) ? (e01 / 0.22f) : powf(1.0f - (e01 - 0.22f) / 0.78f, 1.2f);
            ResArray_DrawVerticalSpire(inst->center, 3.2f * hEnv, 0.28f, hEnv, cam);
            for (int k = 0; k < VC_RES_BLADE_COUNT; k++) {
                ResArray_DrawVerticalSpire(inst->bladePts[k], 2.4f * hEnv, 0.16f, hEnv * 0.90f, cam);
            }
            VFX_ComposeShockRing(inst->center, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_METAL, inst->radius * 1.15f, e01);
        }
        (void)total;
    }
}

#endif /* VC_METAL_RESONANCE_ARRAY_INL */
