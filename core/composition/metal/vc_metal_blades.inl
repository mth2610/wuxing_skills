// vc_metal_blades.inl — Reusable Metal Element Flying-Sword (Thái Ất Thần Binh) VFX
// Included once via metal/metal.inl -> visual_composer.c
//
// 8-plane Bát Diện Hán Kiếm split into 3 static sub-meshes (zero malloc/free):
//   1. Steel Blade (Damascus dark gunmetal steel, high bright-bg darken% + Fresnel rim)
//   2. Imperial Gold Fittings (Tunkou collar, dragon-wing crossguard, grip rings, ring pommel)
//   3. Spiritual Core Gem (Crossguard & pommel glowing cyan-silver core)

#ifndef VC_METAL_BLADES_INL
#define VC_METAL_BLADES_INL

#define VC_METAL_BLADES_MAX_ARRAYS    8
#define VC_METAL_BLADES_MAX_PER_ARRAY 12

typedef struct {
    bool    active;
    bool    launched;
    bool    impacted;
    Vector3 pos;
    Vector3 vel;
    Vector3 forward;
    Vector3 targetPos;
    float   emerge01;
    float   rollDeg;
    float   rollSpeedDeg;
    float   distTraveled;
    float   maxRange;
    float   scale;
    float   slotAngleRad;
    float   slotRadius;
    float   slotHeight;
    float   jitterRollDeg;
    float   phaseOffset;
    float   windTimer;
    Matrix  trailXf;
    int     trailHandle;
} VC_MetalBlade;

typedef struct {
    bool                  active;
    int                   id;
    Vector3               origin;
    Vector3               forward;
    float                 age;
    float                 autoLaunchTimer;
    VFX_MetalBladesConfig config;
    VC_MetalBlade         blades[VC_METAL_BLADES_MAX_PER_ARRAY];
} VC_MetalBladesArray;

static VC_MetalBladesArray s_metalArrays[VC_METAL_BLADES_MAX_ARRAYS];
static int                 s_metalNextId = 1;

// Static sub-mesh storage (zero malloc/free)
typedef struct {
    float *verts;
    float *normals;
    int    maxTris;
    int    triCount;
    Mesh   mesh;
} MetalSubMesh;

#define METAL_STEEL_MAX_TRIS 320
#define METAL_GOLD_MAX_TRIS  320
#define METAL_GEM_MAX_TRIS   64

static float s_steelVerts[METAL_STEEL_MAX_TRIS * 9];
static float s_steelNormals[METAL_STEEL_MAX_TRIS * 9];
static float s_goldVerts[METAL_GOLD_MAX_TRIS * 9];
static float s_goldNormals[METAL_GOLD_MAX_TRIS * 9];
static float s_gemVerts[METAL_GEM_MAX_TRIS * 9];
static float s_gemNormals[METAL_GEM_MAX_TRIS * 9];

static MetalSubMesh s_subSteel = { s_steelVerts, s_steelNormals, METAL_STEEL_MAX_TRIS, 0, {0} };
static MetalSubMesh s_subGold  = { s_goldVerts,  s_goldNormals,  METAL_GOLD_MAX_TRIS,  0, {0} };
static MetalSubMesh s_subGem   = { s_gemVerts,   s_gemNormals,   METAL_GEM_MAX_TRIS,   0, {0} };

static Material s_bladeDrawMat = {0};
static Shader   s_bladeShader  = {0};
static int      s_bladeLocBaseColor = -1;
static int      s_bladeLocRimColor  = -1;
static int      s_bladeLocPartMode  = -1;
static int      s_bladeLocPhase     = -1;
static int      s_bladeLocTime      = -1;
static bool     s_bladeMeshInit = false;

static void MetalSub_PushTri(MetalSubMesh *sub, Vector3 a, Vector3 b, Vector3 c)
{
    if (sub->triCount >= sub->maxTris) return;
    Vector3 ab = Vector3Subtract(b, a);
    Vector3 ac = Vector3Subtract(c, a);
    Vector3 n = Vector3CrossProduct(ab, ac);
    if (Vector3LengthSqr(n) > 1e-10f) n = Vector3Normalize(n);
    else n = (Vector3){ 0.0f, 1.0f, 0.0f };

    int vbase = sub->triCount * 9;
    sub->verts[vbase + 0] = a.x; sub->verts[vbase + 1] = a.y; sub->verts[vbase + 2] = a.z;
    sub->verts[vbase + 3] = b.x; sub->verts[vbase + 4] = b.y; sub->verts[vbase + 5] = b.z;
    sub->verts[vbase + 6] = c.x; sub->verts[vbase + 7] = c.y; sub->verts[vbase + 8] = c.z;

    for (int k = 0; k < 3; k++) {
        sub->normals[vbase + k * 3 + 0] = n.x;
        sub->normals[vbase + k * 3 + 1] = n.y;
        sub->normals[vbase + k * 3 + 2] = n.z;
    }
    sub->triCount++;
}

static void MetalSub_PushQuad(MetalSubMesh *sub, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3)
{
    MetalSub_PushTri(sub, p0, p1, p2);
    MetalSub_PushTri(sub, p0, p2, p3);
}

static void MetalSub_Upload(MetalSubMesh *sub)
{
    sub->mesh.vertexCount = sub->triCount * 3;
    sub->mesh.triangleCount = sub->triCount;
    sub->mesh.vertices = sub->verts;
    sub->mesh.normals = sub->normals;
    UploadMesh(&sub->mesh, false);
}

static void MetalBlade_InitMesh(void)
{
    if (s_bladeMeshInit) return;

    // Sleek, classic Wuxia Jian (Thần Kiếm):
    // Z runs from pommel (-0.54m) through crossguard (-0.22m) to needle tip (+1.05m).
    // Clean 4-plane diamond blade so Fresnel & specular catch every bevel sharply.
    typedef struct {
        float z, halfW, halfT;
    } BladeStation;

    static const BladeStation st[7] = {
        { -0.21f, 0.036f, 0.014f }, // ricasso
        { -0.04f, 0.032f, 0.013f }, // subtle waist
        {  0.30f, 0.034f, 0.012f }, // mid swell
        {  0.65f, 0.030f, 0.010f }, // taper
        {  0.88f, 0.021f, 0.007f }, // shoulder
        {  0.98f, 0.011f, 0.004f }, // tip bevel
        {  1.08f, 0.000f, 0.000f }  // needle point
    };

    // 1. Slender 4-Plane Diamond Blade (Steel SubMesh)
    for (int i = 0; i < 6; i++) {
        const BladeStation *s0 = &st[i];
        const BladeStation *s1 = &st[i + 1];
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                bool flip = (sx * sy < 0);
                Vector3 sp0 = { 0.0f,             sy * s0->halfT, s0->z };
                Vector3 ed0 = { sx * s0->halfW,   0.0f,           s0->z };
                Vector3 sp1 = { 0.0f,             sy * s1->halfT, s1->z };
                Vector3 ed1 = { sx * s1->halfW,   0.0f,           s1->z };

                if (i == 5) {
                    Vector3 tip = { 0.0f, 0.0f, s1->z };
                    if (!flip) MetalSub_PushTri(&s_subSteel, sp0, ed0, tip);
                    else       MetalSub_PushTri(&s_subSteel, ed0, sp0, tip);
                } else {
                    if (!flip) MetalSub_PushQuad(&s_subSteel, sp0, ed0, ed1, sp1);
                    else       MetalSub_PushQuad(&s_subSteel, ed0, sp0, sp1, ed1);
                }
            }
        }
    }

    // 2. Sleek Swept-Wing Wuxia Crossguard (Gold SubMesh)
    // Compact, graceful guard hugging the blade base (no chunky triangles).
    for (int sx = -1; sx <= 1; sx += 2) {
        Vector3 c0  = { 0.0f,       0.024f, -0.25f };
        Vector3 c1  = { sx * 0.055f, 0.020f, -0.24f };
        Vector3 c2  = { sx * 0.115f, 0.008f, -0.17f }; // swept wing tip
        Vector3 c3  = { sx * 0.045f, 0.016f, -0.19f };
        Vector3 c0b = { 0.0f,      -0.024f, -0.25f };
        Vector3 c1b = { sx * 0.055f,-0.020f, -0.24f };
        Vector3 c2b = { sx * 0.115f,-0.008f, -0.17f };
        Vector3 c3b = { sx * 0.045f,-0.016f, -0.19f };

        if (sx > 0) {
            MetalSub_PushQuad(&s_subGold, c0, c1, c2, c3);
            MetalSub_PushQuad(&s_subGold, c1b, c0b, c3b, c2b);
            MetalSub_PushQuad(&s_subGold, c1, c1b, c2b, c2);
            MetalSub_PushQuad(&s_subGold, c3, c2, c2b, c3b);
            MetalSub_PushQuad(&s_subGold, c0, c0b, c1b, c1);
            MetalSub_PushQuad(&s_subGold, c0, c3, c3b, c0b);
        } else {
            MetalSub_PushQuad(&s_subGold, c1, c0, c3, c2);
            MetalSub_PushQuad(&s_subGold, c0b, c1b, c2b, c3b);
            MetalSub_PushQuad(&s_subGold, c1b, c1, c2, c2b);
            MetalSub_PushQuad(&s_subGold, c2, c3, c3b, c2b);
            MetalSub_PushQuad(&s_subGold, c0b, c0, c1, c1b);
            MetalSub_PushQuad(&s_subGold, c3, c0, c0b, c3b);
        }
    }

    // 3. Slim Octagonal Grip (Steel) + Gold Ferrule & Diamond Pommel (Gold)
    const int gripSegs = 8;
    for (int i = 0; i < gripSegs; i++) {
        int ni = (i + 1) % gripSegs;
        float a0 = ((float)i / (float)gripSegs) * 2.0f * PI;
        float a1 = ((float)ni / (float)gripSegs) * 2.0f * PI;
        float r = 0.015f;
        Vector3 g00 = { cosf(a0) * r, sinf(a0) * r * 0.78f, -0.25f };
        Vector3 g01 = { cosf(a1) * r, sinf(a1) * r * 0.78f, -0.25f };
        Vector3 g10 = { cosf(a0) * r, sinf(a0) * r * 0.78f, -0.49f };
        Vector3 g11 = { cosf(a1) * r, sinf(a1) * r * 0.78f, -0.49f };
        MetalSub_PushQuad(&s_subSteel, g00, g01, g11, g10);

        // Pommel flare -> tip
        float rP = 0.026f;
        Vector3 p00 = { cosf(a0) * rP, sinf(a0) * rP * 0.85f, -0.52f };
        Vector3 p01 = { cosf(a1) * rP, sinf(a1) * rP * 0.85f, -0.52f };
        Vector3 pTip = { 0.0f, 0.0f, -0.59f };
        MetalSub_PushQuad(&s_subGold, g10, g11, p01, p00);
        MetalSub_PushTri(&s_subGold, p00, p01, pTip);
    }

    // 4. Center Crossguard Spiritual Jewel (Gem SubMesh)
    Vector3 gF = { 0.0f,  0.0f, -0.195f };
    Vector3 gB = { 0.0f,  0.0f, -0.245f };
    Vector3 gR[4] = {
        {  0.018f,  0.0f,   -0.22f },
        {  0.0f,    0.022f, -0.22f },
        { -0.018f,  0.0f,   -0.22f },
        {  0.0f,   -0.022f, -0.22f }
    };
    for (int k = 0; k < 4; k++) {
        int nk = (k + 1) % 4;
        MetalSub_PushTri(&s_subGem, gR[k], gR[nk], gF);
        MetalSub_PushTri(&s_subGem, gR[nk], gR[k], gB);
    }

    MetalSub_Upload(&s_subSteel);
    MetalSub_Upload(&s_subGold);
    MetalSub_Upload(&s_subGem);

    s_bladeShader       = ResourceManager_LoadShader("core/shaders/metal_blade.vs", "core/shaders/metal_blade.fs");
    s_bladeLocBaseColor = GetShaderLocation(s_bladeShader, "u_baseColor");
    s_bladeLocRimColor  = GetShaderLocation(s_bladeShader, "u_rimColor");
    s_bladeLocPartMode  = GetShaderLocation(s_bladeShader, "u_partMode");
    s_bladeLocPhase     = GetShaderLocation(s_bladeShader, "u_phase");
    s_bladeLocTime      = GetShaderLocation(s_bladeShader, "u_time");

    s_bladeDrawMat = LoadMaterialDefault();
    s_bladeMeshInit = true;
}

static Matrix MetalBlade_BuildTransform(Vector3 pos, Vector3 forward, float rollDeg, float scale)
{
    Vector3 fwd = (Vector3LengthSqr(forward) > 0.0001f)
                      ? Vector3Normalize(forward)
                      : (Vector3){ 0.0f, 0.0f, 1.0f };
    Vector3 upRef = (fabsf(fwd.y) > 0.95f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(upRef, fwd));
    Vector3 up = Vector3CrossProduct(fwd, right);

    Matrix basis = {
        right.x, up.x, fwd.x, 0.0f,
        right.y, up.y, fwd.y, 0.0f,
        right.z, up.z, fwd.z, 0.0f,
        0.0f,    0.0f, 0.0f,  1.0f
    };

    Matrix mScale = MatrixScale(scale, scale, scale);
    Matrix mRoll  = MatrixRotateZ(rollDeg * DEG2RAD);
    Matrix mTrans = MatrixTranslate(pos.x, pos.y, pos.z);
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(mScale, mRoll), basis), mTrans);
}

static VC_MetalBladesArray *MetalBlades_FindByHandle(int handle)
{
    if (handle <= 0) return NULL;
    for (int i = 0; i < VC_METAL_BLADES_MAX_ARRAYS; i++) {
        if (s_metalArrays[i].active && s_metalArrays[i].id == handle) {
            return &s_metalArrays[i];
        }
    }
    return NULL;
}

VFX_MetalBladesConfig VFX_MetalBlades_DefaultConfig(void)
{
    VFX_MetalBladesConfig cfg = {0};
    cfg.bladeCount       = 5;
    cfg.bladeScale       = 1.25f;
    cfg.haloRadius       = 1.55f;
    cfg.haloHeight       = 2.05f;
    cfg.arcDeg           = 140.0f;
    cfg.emergeDuration   = 0.24f;
    cfg.bladeSpeed       = 25.0f;
    cfg.maxRange         = 13.5f;
    cfg.trailWidth       = 0.09f;
    cfg.autoLaunchDelay  = 0.0f;
    cfg.autoLaunchGap    = 0.12f;
    cfg.autoTargetOffset = (Vector3){ 6.0f, 0.9f, 0.0f };
    cfg.bladeColor       = (Color){ 215, 198, 152, 255 };
    cfg.emissionColor    = (Color){ 255, 224, 130, 255 };
    cfg.emissionGain     = 1.0f;
    return cfg;
}

static void MetalBlade_ComputeHoverPose(Vector3 origin, Vector3 fwd, float angle,
                                        float radius, float height,
                                        Vector3 *outPos, Vector3 *outFwd)
{
    Vector3 right = { -fwd.z, 0.0f, fwd.x };
    Vector3 radial = Vector3Add(
        Vector3Scale(right, sinf(angle)),
        Vector3Scale(fwd, 0.55f + 0.35f * cosf(angle))
    );
    *outPos = (Vector3){
        origin.x + radial.x * radius,
        origin.y + height,
        origin.z + radial.z * radius
    };
    *outFwd = Vector3Normalize((Vector3){
        fwd.x + right.x * sinf(angle) * 0.14f,
        -0.06f,
        fwd.z + right.z * sinf(angle) * 0.14f
    });
}

int VFX_ComposeMetalBlades(Vector3 origin, Vector3 forward, const VFX_MetalBladesConfig *cfg)
{
    MetalBlade_InitMesh();

    int slot = -1;
    for (int i = 0; i < VC_METAL_BLADES_MAX_ARRAYS; i++) {
        if (!s_metalArrays[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        for (int b = 0; b < VC_METAL_BLADES_MAX_PER_ARRAY; b++) {
            if (s_metalArrays[slot].blades[b].trailHandle >= 0) {
                VFX_KillTrail(s_metalArrays[slot].blades[b].trailHandle);
            }
        }
    }

    VFX_MetalBladesConfig c = cfg ? *cfg : VFX_MetalBlades_DefaultConfig();
    if (c.bladeCount < 1) c.bladeCount = 1;
    if (c.bladeCount > VC_METAL_BLADES_MAX_PER_ARRAY) c.bladeCount = VC_METAL_BLADES_MAX_PER_ARRAY;
    if (c.bladeScale <= 0.05f) c.bladeScale = 1.0f;

    Vector3 fwd = { forward.x, 0.0f, forward.z };
    if (Vector3LengthSqr(fwd) < 0.0001f) fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    else fwd = Vector3Normalize(fwd);

    VC_MetalBladesArray *arr = &s_metalArrays[slot];
    *arr = (VC_MetalBladesArray){0};
    arr->active          = true;
    arr->id              = s_metalNextId++;
    arr->origin          = origin;
    arr->forward         = fwd;
    arr->config          = c;
    arr->autoLaunchTimer = 0.0f;

    float arcRad = c.arcDeg * DEG2RAD;
    for (int i = 0; i < c.bladeCount; i++) {
        VC_MetalBlade *b = &arr->blades[i];
        float tNorm = (c.bladeCount > 1) ? ((float)i / (float)(c.bladeCount - 1)) - 0.5f : 0.0f;
        float angle = tNorm * arcRad;

        b->active         = true;
        b->launched       = false;
        b->impacted       = false;
        b->slotAngleRad   = angle;
        b->slotRadius     = c.haloRadius;
        b->slotHeight     = c.haloHeight + 0.45f * cosf(tNorm * PI);
        b->jitterRollDeg  = (float)GetRandomValue(-8, 8);
        b->rollDeg        = b->jitterRollDeg;
        b->rollSpeedDeg   = 720.0f;
        b->scale          = c.bladeScale;
        b->phaseOffset    = (float)i * 0.9f;
        b->maxRange       = c.maxRange;
        b->trailHandle    = -1;

        MetalBlade_ComputeHoverPose(origin, fwd, angle, b->slotRadius, b->slotHeight, &b->pos, &b->forward);
        b->trailXf = MatrixTranslate(b->pos.x, b->pos.y, b->pos.z);
    }

    return arr->id;
}

void VFX_MetalBlades_SetOrigin(int handle, Vector3 origin, Vector3 forward)
{
    VC_MetalBladesArray *arr = MetalBlades_FindByHandle(handle);
    if (!arr) return;
    arr->origin = origin;
    Vector3 fwd = { forward.x, 0.0f, forward.z };
    if (Vector3LengthSqr(fwd) > 0.0001f) {
        arr->forward = Vector3Normalize(fwd);
    }
}

int VFX_MetalBlades_LaunchNext(int handle, Vector3 targetPos)
{
    VC_MetalBladesArray *arr = MetalBlades_FindByHandle(handle);
    if (!arr) return -1;

    for (int i = 0; i < arr->config.bladeCount; i++) {
        VC_MetalBlade *b = &arr->blades[i];
        if (!b->active || b->launched || b->impacted) continue;

        b->launched     = true;
        b->targetPos    = targetPos;
        b->distTraveled = 0.0f;
        b->windTimer    = 0.0f;

        Vector3 toTarget = Vector3Subtract(targetPos, b->pos);
        float dist = Vector3Length(toTarget);
        Vector3 dir = (dist > 0.001f) ? Vector3Scale(toTarget, 1.0f / dist) : arr->forward;
        b->forward = dir;
        b->vel     = Vector3Scale(dir, arr->config.bladeSpeed);

        b->trailXf = MetalBlade_BuildTransform(b->pos, b->forward, b->rollDeg, 1.0f);
        b->trailHandle = VFX_ComposeTrail(&b->trailXf, VC_MAT_METAL,
                                          arr->config.trailWidth * b->scale,
                                          0.24f, TRAIL_PRESET_BLADE);
        Wind_SpawnGust(b->pos, b->forward, 1.35f * b->scale, 6.5f, 0.22f);
        return i;
    }
    return -1;
}

bool VFX_MetalBlades_GetBladeState(int handle, int bladeIdx, Vector3 *outPos, bool *outLaunched, bool *outActive)
{
    VC_MetalBladesArray *arr = MetalBlades_FindByHandle(handle);
    if (!arr || bladeIdx < 0 || bladeIdx >= arr->config.bladeCount) return false;
    const VC_MetalBlade *b = &arr->blades[bladeIdx];
    if (outPos)      *outPos      = b->pos;
    if (outLaunched) *outLaunched = b->launched;
    if (outActive)   *outActive   = (b->active && !b->impacted);
    return true;
}

void VFX_MetalBlades_ImpactBlade(int handle, int bladeIdx, Vector3 impactPos)
{
    VC_MetalBladesArray *arr = MetalBlades_FindByHandle(handle);
    if (!arr || bladeIdx < 0 || bladeIdx >= arr->config.bladeCount) return;
    VC_MetalBlade *b = &arr->blades[bladeIdx];
    if (!b->active || b->impacted) return;

    b->pos      = impactPos;
    b->impacted = true;
    b->active   = false;

    if (b->trailHandle >= 0) {
        VFX_TrailSetWidth(b->trailHandle, 0.0f);
        VFX_KillTrail(b->trailHandle);
        b->trailHandle = -1;
    }

    VFX_ComposeContactSparkMode(impactPos, VC_MAT_METAL, 0.85f * b->scale, 0.90f, CONTACT_SPARK_CENTRIFUGAL);
    Wind_SpawnRadialBlast(impactPos, 1.85f * b->scale, 9.5f, 0.30f);
}

void VFX_MetalBlades_Kill(int handle)
{
    VC_MetalBladesArray *arr = MetalBlades_FindByHandle(handle);
    if (!arr) return;
    for (int i = 0; i < arr->config.bladeCount; i++) {
        if (arr->blades[i].trailHandle >= 0) {
            VFX_KillTrail(arr->blades[i].trailHandle);
            arr->blades[i].trailHandle = -1;
        }
        arr->blades[i].active = false;
    }
    arr->active = false;
}

void VC_MetalBlades_Update(float dt)
{
    for (int a = 0; a < VC_METAL_BLADES_MAX_ARRAYS; a++) {
        VC_MetalBladesArray *arr = &s_metalArrays[a];
        if (!arr->active) continue;

        arr->age += dt;
        Vector3 fwd = arr->forward;
        Vector3 right = { -fwd.z, 0.0f, fwd.x };

        if (arr->config.autoLaunchDelay > 0.0f && arr->age >= arr->config.autoLaunchDelay) {
            arr->autoLaunchTimer += dt;
            if (arr->autoLaunchTimer >= arr->config.autoLaunchGap) {
                arr->autoLaunchTimer -= arr->config.autoLaunchGap;
                Vector3 tgt = Vector3Add(arr->origin, arr->config.autoTargetOffset);
                VFX_MetalBlades_LaunchNext(arr->id, tgt);
            }
        }

        int aliveCount = 0;
        for (int i = 0; i < arr->config.bladeCount; i++) {
            VC_MetalBlade *b = &arr->blades[i];
            if (!b->active || b->impacted) continue;
            aliveCount++;

            float emergeTime = (arr->config.emergeDuration > 0.01f) ? arr->config.emergeDuration : 0.01f;
            b->emerge01 = Clamp((arr->age - (float)i * 0.035f) / emergeTime, 0.0f, 1.0f);

            if (!b->launched) {
                float bob = sinf(arr->age * 3.6f + b->phaseOffset) * 0.06f;
                MetalBlade_ComputeHoverPose(arr->origin, fwd, b->slotAngleRad,
                                            b->slotRadius, b->slotHeight + bob,
                                            &b->pos, &b->forward);
                b->rollDeg = b->jitterRollDeg + sinf(arr->age * 2.0f + b->phaseOffset) * 3.5f;
            } else {
                float stepDist = Vector3Length(b->vel) * dt;
                b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
                b->distTraveled += stepDist;
                b->rollDeg += b->rollSpeedDeg * dt;
                b->trailXf = MetalBlade_BuildTransform(b->pos, b->forward, b->rollDeg, 1.0f);
                b->windTimer += dt;
                if (b->windTimer >= 0.08f) {
                    b->windTimer -= 0.08f;
                    Wind_SpawnGust(b->pos, b->forward, 1.15f * b->scale, 5.5f, 0.14f);
                }

                float groundY = VFX_GroundHeightFromMap(b->pos.x, b->pos.z, NULL);
                if (b->pos.y <= groundY + 0.05f || b->distTraveled >= b->maxRange) {
                    if (b->pos.y < groundY + 0.05f) b->pos.y = groundY + 0.05f;
                    VFX_MetalBlades_ImpactBlade(arr->id, i, b->pos);
                }
            }
        }

        if (aliveCount == 0 && arr->age > 0.35f) {
            arr->active = false;
        }
    }
}

void VC_MetalBlades_DrawShadowPass(void)
{
    MetalBlade_InitMesh();
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0 || s_subSteel.mesh.vertexCount == 0) return;
    s_bladeDrawMat.shader = depthShader;

    for (int a = 0; a < VC_METAL_BLADES_MAX_ARRAYS; a++) {
        const VC_MetalBladesArray *arr = &s_metalArrays[a];
        if (!arr->active) continue;

        for (int i = 0; i < arr->config.bladeCount; i++) {
            const VC_MetalBlade *b = &arr->blades[i];
            if (!b->active || b->impacted || b->emerge01 <= 0.05f) continue;

            float s = b->scale * b->emerge01;
            Matrix xf = MetalBlade_BuildTransform(b->pos, b->forward, b->rollDeg, s);
            DrawMesh(s_subSteel.mesh, s_bladeDrawMat, xf);
            DrawMesh(s_subGold.mesh,  s_bladeDrawMat, xf);
        }
    }
}

void VC_MetalBlades_Draw3D(Camera3D cam)
{
    (void)cam;
    MetalBlade_InitMesh();
    if (s_subSteel.mesh.vertexCount == 0) return;

    // 1. Ground contact shadows
    Environment_BeginShadowBatch();
    for (int a = 0; a < VC_METAL_BLADES_MAX_ARRAYS; a++) {
        const VC_MetalBladesArray *arr = &s_metalArrays[a];
        if (!arr->active) continue;
        for (int i = 0; i < arr->config.bladeCount; i++) {
            const VC_MetalBlade *b = &arr->blades[i];
            if (!b->active || b->impacted || b->emerge01 <= 0.05f) continue;
            float s = 0.36f * b->scale * b->emerge01;
            Environment_DrawSmartShadow(b->pos, ENV_SHAPE_SPHERE, s, s * 1.4f);
        }
    }
    Environment_EndShadowBatch();

    // 2. 3D Sleek Wuxia Jian (Dedicated Metallic Specular + Fresnel Edge-Bloom Shader)
    if (s_bladeShader.id == 0) return;
    s_bladeDrawMat.shader = s_bladeShader;

    float now = (float)GetTime();
    if (s_bladeLocTime >= 0) SetShaderValue(s_bladeShader, s_bladeLocTime, &now, SHADER_UNIFORM_FLOAT);

    for (int a = 0; a < VC_METAL_BLADES_MAX_ARRAYS; a++) {
        const VC_MetalBladesArray *arr = &s_metalArrays[a];
        if (!arr->active) continue;

        float bladeBase[4] = { 0.56f, 0.60f, 0.68f, 1.0f }; // sleek dark-silver core so Fresnel + specular pop
        float bladeRim[4]  = {
            (float)arr->config.emissionColor.r / 255.0f,
            (float)arr->config.emissionColor.g / 255.0f,
            (float)arr->config.emissionColor.b / 255.0f,
            1.0f
        };
        float goldBase[4]  = { 0.85f, 0.65f, 0.22f, 1.0f };
        float goldRim[4]   = { 1.00f, 0.88f, 0.45f, 1.0f };
        float gemBase[4]   = { 1.00f, 0.96f, 0.82f, 1.0f };
        float gemRim[4]    = { 1.00f, 0.94f, 0.72f, 1.0f };

        for (int i = 0; i < arr->config.bladeCount; i++) {
            const VC_MetalBlade *b = &arr->blades[i];
            if (!b->active || b->impacted || b->emerge01 <= 0.05f) continue;

            float s = b->scale * b->emerge01;
            Matrix xf = MetalBlade_BuildTransform(b->pos, b->forward, b->rollDeg, s);
            if (s_bladeLocPhase >= 0) SetShaderValue(s_bladeShader, s_bladeLocPhase, &b->phaseOffset, SHADER_UNIFORM_FLOAT);

            // Pass A: Slender 4-Plane Diamond Blade
            float mode = 0.0f;
            if (s_bladeLocPartMode >= 0)  SetShaderValue(s_bladeShader, s_bladeLocPartMode,  &mode,     SHADER_UNIFORM_FLOAT);
            if (s_bladeLocBaseColor >= 0) SetShaderValue(s_bladeShader, s_bladeLocBaseColor, bladeBase, SHADER_UNIFORM_VEC4);
            if (s_bladeLocRimColor >= 0)  SetShaderValue(s_bladeShader, s_bladeLocRimColor,  bladeRim,  SHADER_UNIFORM_VEC4);
            DrawMesh(s_subSteel.mesh, s_bladeDrawMat, xf);

            // Pass B: Sleek Imperial Gold Crossguard & Pommel
            mode = 1.0f;
            if (s_bladeLocPartMode >= 0)  SetShaderValue(s_bladeShader, s_bladeLocPartMode,  &mode,    SHADER_UNIFORM_FLOAT);
            if (s_bladeLocBaseColor >= 0) SetShaderValue(s_bladeShader, s_bladeLocBaseColor, goldBase, SHADER_UNIFORM_VEC4);
            if (s_bladeLocRimColor >= 0)  SetShaderValue(s_bladeShader, s_bladeLocRimColor,  goldRim,  SHADER_UNIFORM_VEC4);
            DrawMesh(s_subGold.mesh, s_bladeDrawMat, xf);

            // Pass C: Center Spiritual Jewel
            mode = 2.0f;
            if (s_bladeLocPartMode >= 0)  SetShaderValue(s_bladeShader, s_bladeLocPartMode,  &mode,   SHADER_UNIFORM_FLOAT);
            if (s_bladeLocBaseColor >= 0) SetShaderValue(s_bladeShader, s_bladeLocBaseColor, gemBase, SHADER_UNIFORM_VEC4);
            if (s_bladeLocRimColor >= 0)  SetShaderValue(s_bladeShader, s_bladeLocRimColor,  gemRim,  SHADER_UNIFORM_VEC4);
            DrawMesh(s_subGem.mesh, s_bladeDrawMat, xf);
        }
    }
}

#endif // VC_METAL_BLADES_INL
