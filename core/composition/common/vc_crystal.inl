// ============================================================================
// vc_crystal.inl — Primary VFX: Generalized Faceted Crystal & 3D Shatter Shards
// ============================================================================
// Single-crystal primary (`VFX_Crystal_Spawn`) + 3D tumbling faceted shard
// primary (`VFX_CrystalShatter_Spawn`). Supports Ice, Jade, Ruby, Amethyst,
// Gold, Steel, and Obsidian via `VFX_CrystalStyle`, with flat geometric facet
// normals, Beer-Lambert absorption / metallic reflectance, and real directional
// shadow-map casting (`VC_Crystal_DrawShadowPass`).
// Included once by core/composition/common/common.inl.
// ============================================================================

#define VC_CRYSTAL_POOL_SIZE       64
#define VC_CRYSTAL_SHARD_POOL_SIZE 96
#define VC_CRYSTAL_TMPL_HEIGHT     1.30f
#define VC_CRYSTAL_TMPL_RADIUS     0.16f

typedef struct {
    bool active;
    Vector3 pos;
    Vector3 dir;
    float height;
    float radius;
    float eruptTime;
    float holdTime;
    float fadeTime;
    float delay;
    float rollDeg;
    VFX_CrystalStyle style;
    bool castShadow;
    bool shatterOnEnd;
    bool shattered;
    float age;
} CrystalPrimarySlot;

typedef struct {
    bool active;
    Vector3 pos;
    Vector3 vel;
    Vector3 rotAxis;
    float rotAngle;
    float rotSpeed;
    float scale;
    float age;
    float life;
    VFX_CrystalStyle style;
} CrystalShardSlot;

static CrystalPrimarySlot s_crystalPool[VC_CRYSTAL_POOL_SIZE] = {0};
static CrystalShardSlot   s_shardPool[VC_CRYSTAL_SHARD_POOL_SIZE] = {0};
static CrystalMaterial    s_crystalStyleMats[VFX_CRYSTAL_STYLE_COUNT] = {0};
static bool               s_crystalStyleLoaded[VFX_CRYSTAL_STYLE_COUNT] = {0};
static int                s_crystalSceneLoc[VFX_CRYSTAL_STYLE_COUNT];
static int                s_crystalHasSceneLoc[VFX_CRYSTAL_STYLE_COUNT];
static int                s_crystalBackFaceLoc[VFX_CRYSTAL_STYLE_COUNT];
static Mesh               s_crystalTmplMesh = {0};
static bool               s_crystalTmplReady = false;
static unsigned int       s_crystalRng = 0x5EED1CE0u;

static float Crystal_Rand01(void) {
    s_crystalRng ^= s_crystalRng << 13;
    s_crystalRng ^= s_crystalRng >> 17;
    s_crystalRng ^= s_crystalRng << 5;
    return (float)(s_crystalRng & 0x00FFFFFFu) / 16777215.0f;
}

#define VC_CRYSTAL_OBELISK_SIDES 6
#define VC_CRYSTAL_OBELISK_TRIS  (VC_CRYSTAL_OBELISK_SIDES * 5) /* 2 rings * 2 tris + 1 tip tri per side */

static void VC_Crystal_EmitTri(float *verts, float *norms, float *uvs, int *triIdx,
                               Vector3 a, Vector3 b, Vector3 c,
                               Vector2 ua, Vector2 ub, Vector2 uc) {
    Vector3 e1 = Vector3Subtract(b, a);
    Vector3 e2 = Vector3Subtract(c, a);
    Vector3 n  = Vector3Normalize(Vector3CrossProduct(e1, e2));
    int base = (*triIdx) * 3;
    Vector3 pts[3] = { a, b, c };
    Vector2 coords[3] = { ua, ub, uc };
    for (int k = 0; k < 3; ++k) {
        int vi = base + k;
        verts[vi * 3 + 0] = pts[k].x;
        verts[vi * 3 + 1] = pts[k].y;
        verts[vi * 3 + 2] = pts[k].z;
        norms[vi * 3 + 0] = n.x;
        norms[vi * 3 + 1] = n.y;
        norms[vi * 3 + 2] = n.z;
        uvs[vi * 2 + 0] = coords[k].x;
        uvs[vi * 2 + 1] = coords[k].y;
    }
    (*triIdx)++;
}

static Mesh VC_Crystal_GetTemplateMesh(void) {
    if (!s_crystalTmplReady) {
        const float H = VC_CRYSTAL_TMPL_HEIGHT;
        const float R = VC_CRYSTAL_TMPL_RADIUS;
        const float ySkirt    = -0.18f * H;
        const float yBase     =  0.00f;
        const float yShoulder =  0.68f * H;
        const float yApex     =  1.00f * H;

        /* Asymmetric natural quartz/ice facet radii & slight angular offsets */
        const float radScale[VC_CRYSTAL_OBELISK_SIDES] = { 1.00f, 0.88f, 1.06f, 0.94f, 1.04f, 0.90f };
        const float angJitter[VC_CRYSTAL_OBELISK_SIDES] = { 0.00f, 0.08f, -0.05f, 0.06f, -0.07f, 0.04f };

        Vector3 ringSkirt[VC_CRYSTAL_OBELISK_SIDES];
        Vector3 ringBase[VC_CRYSTAL_OBELISK_SIDES];
        Vector3 ringShoulder[VC_CRYSTAL_OBELISK_SIDES];
        Vector3 apex = { 0.03f * R, yApex, -0.02f * R };

        for (int j = 0; j < VC_CRYSTAL_OBELISK_SIDES; ++j) {
            float ang = ((float)j / (float)VC_CRYSTAL_OBELISK_SIDES) * 2.0f * PI + angJitter[j];
            float r0 = R * radScale[j];
            float c = cosf(ang);
            float s = sinf(ang);
            ringSkirt[j]    = (Vector3){ c * r0 * 1.08f, ySkirt,    s * r0 * 1.08f };
            ringBase[j]     = (Vector3){ c * r0 * 1.00f, yBase,     s * r0 * 1.00f };
            ringShoulder[j] = (Vector3){ c * r0 * 0.68f, yShoulder, s * r0 * 0.68f };
        }

        int vertCount = VC_CRYSTAL_OBELISK_TRIS * 3;
        s_crystalTmplMesh.vertices  = (float *)MemAlloc((unsigned int)vertCount * 3 * sizeof(float));
        s_crystalTmplMesh.normals   = (float *)MemAlloc((unsigned int)vertCount * 3 * sizeof(float));
        s_crystalTmplMesh.texcoords = (float *)MemAlloc((unsigned int)vertCount * 2 * sizeof(float));

        int triIdx = 0;
        for (int j = 0; j < VC_CRYSTAL_OBELISK_SIDES; ++j) {
            int jNext = (j + 1) % VC_CRYSTAL_OBELISK_SIDES;

            /* Buried root skirt: CCW outward (v1, v3, v2), (v1, v4, v3) */
            VC_Crystal_EmitTri(s_crystalTmplMesh.vertices, s_crystalTmplMesh.normals, s_crystalTmplMesh.texcoords, &triIdx,
                               ringSkirt[j], ringBase[jNext], ringSkirt[jNext],
                               (Vector2){0.0f, 0.0f}, (Vector2){1.0f, 0.05f}, (Vector2){1.0f, 0.0f});
            VC_Crystal_EmitTri(s_crystalTmplMesh.vertices, s_crystalTmplMesh.normals, s_crystalTmplMesh.texcoords, &triIdx,
                               ringSkirt[j], ringBase[j], ringBase[jNext],
                               (Vector2){0.0f, 0.0f}, (Vector2){0.0f, 0.05f}, (Vector2){1.0f, 0.05f});

            /* Prismatic shaft: per-face UV.x in [0..1] for crisp frosted facet bevels */
            VC_Crystal_EmitTri(s_crystalTmplMesh.vertices, s_crystalTmplMesh.normals, s_crystalTmplMesh.texcoords, &triIdx,
                               ringBase[j], ringShoulder[jNext], ringBase[jNext],
                               (Vector2){0.0f, 0.05f}, (Vector2){1.0f, 0.68f}, (Vector2){1.0f, 0.05f});
            VC_Crystal_EmitTri(s_crystalTmplMesh.vertices, s_crystalTmplMesh.normals, s_crystalTmplMesh.texcoords, &triIdx,
                               ringBase[j], ringShoulder[j], ringShoulder[jNext],
                               (Vector2){0.0f, 0.05f}, (Vector2){0.0f, 0.68f}, (Vector2){1.0f, 0.68f});

            /* Pyramidal chisel tip: CCW outward (shoulder[j], apex, shoulder[jNext]) */
            VC_Crystal_EmitTri(s_crystalTmplMesh.vertices, s_crystalTmplMesh.normals, s_crystalTmplMesh.texcoords, &triIdx,
                               ringShoulder[j], apex, ringShoulder[jNext],
                               (Vector2){0.0f, 0.68f}, (Vector2){0.5f, 1.00f}, (Vector2){1.0f, 0.68f});
        }

        s_crystalTmplMesh.vertexCount   = triIdx * 3;
        s_crystalTmplMesh.triangleCount = triIdx;
        UploadMesh(&s_crystalTmplMesh, false);
        s_crystalTmplReady = true;
    }
    return s_crystalTmplMesh;
}

static CrystalMaterialParams CrystalStyle_GetParams(VFX_CrystalStyle style) {
    CrystalMaterialParams p = {0};
    switch (style) {
        case VFX_CRYSTAL_STYLE_JADE:
            p.baseColor      = (Color){ 36, 165, 108, 225 };
            p.edgeColor      = (Color){ 170, 255, 205, 255 };
            p.roughness      = 0.22f;
            p.fresnel        = 0.72f;
            p.refraction     = 0.14f;
            p.sparkle        = 0.18f;
            p.crack          = 0.15f;
            p.emission       = 0.08f;
            p.thickness      = 2.10f;
            p.metallic       = 0.00f;
            p.facetSharpness = 0.88f;
            break;
        case VFX_CRYSTAL_STYLE_RUBY:
            p.baseColor      = (Color){ 185, 18, 42, 225 };
            p.edgeColor      = (Color){ 255, 130, 145, 255 };
            p.roughness      = 0.12f;
            p.fresnel        = 0.86f;
            p.refraction     = 0.24f;
            p.sparkle        = 0.45f;
            p.crack          = 0.12f;
            p.emission       = 0.12f;
            p.thickness      = 1.90f;
            p.metallic       = 0.00f;
            p.facetSharpness = 0.96f;
            break;
        case VFX_CRYSTAL_STYLE_AMETHYST:
            p.baseColor      = (Color){ 118, 38, 198, 225 };
            p.edgeColor      = (Color){ 228, 170, 255, 255 };
            p.roughness      = 0.14f;
            p.fresnel        = 0.82f;
            p.refraction     = 0.20f;
            p.sparkle        = 0.40f;
            p.crack          = 0.18f;
            p.emission       = 0.12f;
            p.thickness      = 1.80f;
            p.metallic       = 0.00f;
            p.facetSharpness = 0.95f;
            break;
        case VFX_CRYSTAL_STYLE_GOLD:
            p.baseColor      = (Color){ 245, 195, 65, 255 };
            p.edgeColor      = (Color){ 255, 240, 165, 255 };
            p.roughness      = 0.18f;
            p.fresnel        = 0.92f;
            p.refraction     = 0.00f;
            p.sparkle        = 0.35f;
            p.crack          = 0.00f;
            p.emission       = 0.05f;
            p.thickness      = 1.00f;
            p.metallic       = 1.00f;
            p.facetSharpness = 0.90f;
            break;
        case VFX_CRYSTAL_STYLE_STEEL:
            p.baseColor      = (Color){ 175, 192, 208, 255 };
            p.edgeColor      = (Color){ 238, 246, 255, 255 };
            p.roughness      = 0.22f;
            p.fresnel        = 0.88f;
            p.refraction     = 0.00f;
            p.sparkle        = 0.28f;
            p.crack          = 0.00f;
            p.emission       = 0.00f;
            p.thickness      = 1.00f;
            p.metallic       = 1.00f;
            p.facetSharpness = 0.92f;
            break;
        case VFX_CRYSTAL_STYLE_OBSIDIAN:
            p.baseColor      = (Color){ 28, 22, 38, 250 };
            p.edgeColor      = (Color){ 140, 90, 195, 255 };
            p.roughness      = 0.15f;
            p.fresnel        = 0.80f;
            p.refraction     = 0.05f;
            p.sparkle        = 0.30f;
            p.crack          = 0.22f;
            p.emission       = 0.08f;
            p.thickness      = 2.80f;
            p.metallic       = 0.18f;
            p.facetSharpness = 0.96f;
            break;
        case VFX_CRYSTAL_STYLE_ICE:
        default:
            p.baseColor      = (Color){ 24, 92, 165, 118 };
            p.edgeColor      = (Color){ 195, 245, 255, 255 };
            p.roughness      = 0.10f;
            p.fresnel        = 0.90f;
            p.refraction     = 0.25f;
            p.sparkle        = 0.22f;
            p.crack          = 0.55f;
            p.emission       = 0.14f;
            p.thickness      = 1.45f;
            p.metallic       = 0.00f;
            p.facetSharpness = 0.96f;
            break;
    }
    return p;
}

static CrystalMaterial VC_Crystal_GetMaterial(VFX_CrystalStyle style) {
    int idx = ((int)style >= 0 && style < VFX_CRYSTAL_STYLE_COUNT) ? (int)style : 0;
    if (!s_crystalStyleLoaded[idx]) {
        CrystalMaterialParams p = CrystalStyle_GetParams((VFX_CrystalStyle)idx);
        CrystalMaterial_Load(&s_crystalStyleMats[idx], &p);
        s_crystalSceneLoc[idx]    = GetShaderLocation(s_crystalStyleMats[idx].shader, "u_sceneTex");
        s_crystalHasSceneLoc[idx] = GetShaderLocation(s_crystalStyleMats[idx].shader, "u_hasScene");
        s_crystalBackFaceLoc[idx] = GetShaderLocation(s_crystalStyleMats[idx].shader, "u_isBackFace");
        s_crystalStyleLoaded[idx] = true;
    }
    return s_crystalStyleMats[idx];
}

static const char *s_crystalStyleNames[VFX_CRYSTAL_STYLE_COUNT] = {
    "Ice",
    "Jade",
    "Ruby",
    "Amethyst",
    "Gold",
    "Steel",
    "Obsidian"
};

const char *VFX_CrystalStyle_Name(VFX_CrystalStyle style) {
    if ((int)style < 0 || style >= VFX_CRYSTAL_STYLE_COUNT) return "Unknown";
    return s_crystalStyleNames[style];
}

VFX_CrystalDesc VFX_Crystal_DefaultDesc(VFX_CrystalStyle style) {
    VFX_CrystalDesc d = {0};
    d.pos          = (Vector3){0.0f, 0.0f, 0.0f};
    d.dir          = (Vector3){0.0f, 1.0f, 0.0f};
    d.height       = 1.6f;
    d.radius       = 0.22f;
    d.eruptTime    = 0.16f;
    d.holdTime     = 1.15f;
    d.fadeTime     = 0.35f;
    d.delay        = 0.0f;
    d.rollDeg      = 0.0f;
    d.style        = style;
    d.castShadow   = true;
    d.shatterOnEnd = true;
    return d;
}

int VFX_Crystal_Spawn(const VFX_CrystalDesc *desc) {
    if (!desc) return -1;
    int chosen = -1;
    float oldestAge = -1.0f;
    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        if (!s_crystalPool[i].active) {
            chosen = i;
            break;
        }
        if (s_crystalPool[i].age > oldestAge) {
            oldestAge = s_crystalPool[i].age;
            chosen = i;
        }
    }
    if (chosen < 0) return -1;

    CrystalPrimarySlot *slot = &s_crystalPool[chosen];
    memset(slot, 0, sizeof(*slot));
    slot->active       = true;
    slot->pos          = desc->pos;
    slot->dir          = (Vector3LengthSqr(desc->dir) > 0.0001f)
                           ? Vector3Normalize(desc->dir)
                           : (Vector3){0.0f, 1.0f, 0.0f};
    slot->height       = fmaxf(desc->height, 0.15f);
    slot->radius       = fmaxf(desc->radius, 0.03f);
    slot->eruptTime    = fmaxf(desc->eruptTime, 0.02f);
    slot->holdTime     = fmaxf(desc->holdTime, 0.02f);
    slot->fadeTime     = fmaxf(desc->fadeTime, 0.02f);
    slot->delay        = fmaxf(desc->delay, 0.0f);
    slot->rollDeg      = desc->rollDeg;
    slot->style        = ((int)desc->style >= 0 && desc->style < VFX_CRYSTAL_STYLE_COUNT)
                           ? desc->style : VFX_CRYSTAL_STYLE_ICE;
    slot->castShadow   = desc->castShadow;
    slot->shatterOnEnd = desc->shatterOnEnd;
    slot->shattered    = false;
    slot->age          = 0.0f;
    return chosen;
}

void VFX_Crystal_Kill(int handle) {
    if (handle >= 0 && handle < VC_CRYSTAL_POOL_SIZE) {
        s_crystalPool[handle].active = false;
    }
}

void VC_Crystal_ClearAll(void) {
    memset(s_crystalPool, 0, sizeof(s_crystalPool));
}

int VC_Crystal_GetActiveCount(void) {
    int count = 0;
    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        if (s_crystalPool[i].active) count++;
    }
    return count;
}

void VFX_CrystalShatter_Spawn(Vector3 center, Vector3 impulseDir,
                              float spreadSpeed, int count,
                              float shardScale, VFX_CrystalStyle style) {
    if (count <= 0) return;
    Vector3 baseDir = (Vector3LengthSqr(impulseDir) > 0.0001f)
                          ? Vector3Normalize(impulseDir)
                          : (Vector3){0.0f, 1.0f, 0.0f};
    float speed = fmaxf(spreadSpeed, 0.5f);
    float sScale = fmaxf(shardScale, 0.05f);

    for (int n = 0; n < count; ++n) {
        int chosen = -1;
        float oldestAge = -1.0f;
        for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
            if (!s_shardPool[i].active) {
                chosen = i;
                break;
            }
            if (s_shardPool[i].age > oldestAge) {
                oldestAge = s_shardPool[i].age;
                chosen = i;
            }
        }
        if (chosen < 0) break;

        float az = Crystal_Rand01() * 2.0f * PI;
        float rHoriz = 0.45f + 0.55f * Crystal_Rand01();
        float upBoost = 0.55f + 0.85f * Crystal_Rand01();
        Vector3 radial = (Vector3){
            cosf(az) * rHoriz + baseDir.x * 0.4f,
            upBoost + baseDir.y * 0.5f,
            sinf(az) * rHoriz + baseDir.z * 0.4f
        };
        radial = Vector3Normalize(radial);

        CrystalShardSlot *sh = &s_shardPool[chosen];
        sh->active   = true;
        sh->pos      = (Vector3){
            center.x + (Crystal_Rand01() - 0.5f) * sScale * 1.4f,
            center.y + Crystal_Rand01() * sScale * 1.2f,
            center.z + (Crystal_Rand01() - 0.5f) * sScale * 1.4f
        };
        sh->vel      = Vector3Scale(radial, speed * (0.55f + 0.70f * Crystal_Rand01()));
        sh->rotAxis  = Vector3Normalize((Vector3){
            Crystal_Rand01() - 0.5f,
            Crystal_Rand01() - 0.5f,
            Crystal_Rand01() - 0.5f
        });
        if (Vector3LengthSqr(sh->rotAxis) < 0.001f) sh->rotAxis = (Vector3){1.0f, 0.0f, 0.0f};
        sh->rotAngle = Crystal_Rand01() * 2.0f * PI;
        sh->rotSpeed = (4.0f + 8.0f * Crystal_Rand01()) * (Crystal_Rand01() > 0.5f ? 1.0f : -1.0f);
        sh->scale    = sScale * (0.65f + 0.75f * Crystal_Rand01());
        sh->age      = 0.0f;
        sh->life     = 0.75f + 0.65f * Crystal_Rand01();
        sh->style    = style;
    }
}

int VC_CrystalShatter_GetActiveCount(void) {
    int count = 0;
    for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
        if (s_shardPool[i].active) count++;
    }
    return count;
}

void VC_CrystalShatter_ClearAll(void) {
    memset(s_shardPool, 0, sizeof(s_shardPool));
}

void VC_Crystal_Update(float dt) {
    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        CrystalPrimarySlot *slot = &s_crystalPool[i];
        if (!slot->active) continue;
        slot->age += dt;
        float activeAge = slot->age - slot->delay;
        float fadeStart = slot->eruptTime + slot->holdTime;
        if (activeAge >= fadeStart && !slot->shattered && slot->shatterOnEnd) {
            slot->shattered = true;
            Vector3 burstOrigin = Vector3Add(slot->pos, Vector3Scale(slot->dir, slot->height * 0.45f));
            VFX_CrystalShatter_Spawn(burstOrigin, slot->dir, 4.2f, 7,
                                     fmaxf(slot->radius * 0.75f, 0.10f), slot->style);
        }
        if (activeAge >= fadeStart + slot->fadeTime) {
            slot->active = false;
        }
    }

    for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
        CrystalShardSlot *sh = &s_shardPool[i];
        if (!sh->active) continue;
        sh->age += dt;
        if (sh->age >= sh->life) {
            sh->active = false;
            continue;
        }
        sh->vel.y -= 9.81f * dt;
        sh->pos = Vector3Add(sh->pos, Vector3Scale(sh->vel, dt));
        sh->rotAngle += sh->rotSpeed * dt;

        float groundY = MapManager_GetGroundHeightAt(sh->pos.x, sh->pos.z);
        if (!isfinite(groundY)) groundY = 0.0f;
        if (sh->pos.y < groundY + 0.03f) {
            sh->pos.y = groundY + 0.03f;
            if (sh->vel.y < 0.0f) {
                sh->vel.y = -sh->vel.y * 0.34f;
                sh->vel.x *= 0.72f;
                sh->vel.z *= 0.72f;
                sh->rotSpeed *= 0.55f;
            }
        }
    }

    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        if (s_crystalPool[i].active &&
            s_crystalPool[i].style != VFX_CRYSTAL_STYLE_GOLD &&
            s_crystalPool[i].style != VFX_CRYSTAL_STYLE_STEEL) {
            SceneTargets_RequestSceneSnapshot();
            return;
        }
    }
    for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
        if (s_shardPool[i].active &&
            s_shardPool[i].style != VFX_CRYSTAL_STYLE_GOLD &&
            s_shardPool[i].style != VFX_CRYSTAL_STYLE_STEEL) {
            SceneTargets_RequestSceneSnapshot();
            return;
        }
    }
}

static float VC_Crystal_EvalGrow(const CrystalPrimarySlot *slot) {
    float t = slot->age - slot->delay;
    if (t <= 0.0f) return 0.0f;
    if (t < slot->eruptTime) {
        float u = t / slot->eruptTime;
        float inv = 1.0f - u;
        return 0.04f + 0.96f * (1.0f - inv * inv * inv);
    }
    float fadeStart = slot->eruptTime + slot->holdTime;
    if (t <= fadeStart) return 1.0f;
    float f = fminf(1.0f, (t - fadeStart) / slot->fadeTime);
    return fmaxf(0.0f, 1.0f - f * f);
}

static Matrix VC_Crystal_BuildMatrix(const CrystalPrimarySlot *slot, float grow) {
    float sXZ = (slot->radius / VC_CRYSTAL_TMPL_RADIUS) * (0.75f + 0.25f * grow);
    float sY  = (slot->height / VC_CRYSTAL_TMPL_HEIGHT) * grow;
    Matrix S = MatrixScale(sXZ, sY, sXZ);

    Quaternion qAlign = QuaternionFromVector3ToVector3((Vector3){0.0f, 1.0f, 0.0f}, slot->dir);
    Quaternion qRoll  = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, slot->rollDeg * DEG2RAD);
    Matrix R = QuaternionToMatrix(QuaternionMultiply(qAlign, qRoll));
    Matrix T = MatrixTranslate(slot->pos.x, slot->pos.y, slot->pos.z);
    return MatrixMultiply(MatrixMultiply(S, R), T);
}

static void VC_Crystal_DrawStyleMeshes(int st, Mesh tmpl, Material passMat) {
    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        const CrystalPrimarySlot *slot = &s_crystalPool[i];
        if (!slot->active || (int)slot->style != st) continue;
        float grow = VC_Crystal_EvalGrow(slot);
        if (grow <= 0.01f) continue;
        Matrix m = VC_Crystal_BuildMatrix(slot, grow);
        DrawMesh(tmpl, passMat, m);
    }

    for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
        const CrystalShardSlot *sh = &s_shardPool[i];
        if (!sh->active || (int)sh->style != st) continue;
        float t = sh->age / sh->life;
        float shrink = (t < 0.72f) ? 1.0f : fmaxf(0.0f, 1.0f - (t - 0.72f) / 0.28f);
        if (shrink <= 0.01f) continue;
        float s = (sh->scale / VC_CRYSTAL_TMPL_RADIUS) * 0.42f * shrink;
        Matrix S = MatrixScale(s, s * 1.45f, s);
        Matrix R = MatrixRotate(sh->rotAxis, sh->rotAngle);
        Matrix T = MatrixTranslate(sh->pos.x, sh->pos.y, sh->pos.z);
        DrawMesh(tmpl, passMat, MatrixMultiply(MatrixMultiply(S, R), T));
    }
}

static bool VC_Crystal_HasStyleActive(int st) {
    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        if (s_crystalPool[i].active && (int)s_crystalPool[i].style == st &&
            s_crystalPool[i].age >= s_crystalPool[i].delay) {
            return true;
        }
    }
    for (int i = 0; i < VC_CRYSTAL_SHARD_POOL_SIZE; ++i) {
        if (s_shardPool[i].active && (int)s_shardPool[i].style == st) {
            return true;
        }
    }
    return false;
}

static bool VC_Crystal_IsStyleDielectric(int st) {
    return (st != VFX_CRYSTAL_STYLE_GOLD && st != VFX_CRYSTAL_STYLE_STEEL);
}

void VC_Crystal_Draw3D(Camera3D camera) {
    (void)camera;
    if (VC_Crystal_GetActiveCount() <= 0 && VC_CrystalShatter_GetActiveCount() <= 0) return;

    Mesh tmpl = VC_Crystal_GetTemplateMesh();

    for (int st = 0; st < VFX_CRYSTAL_STYLE_COUNT; ++st) {
        if (VC_Crystal_IsStyleDielectric(st) || !VC_Crystal_HasStyleActive(st)) continue;

        CrystalMaterial mat = VC_Crystal_GetMaterial((VFX_CrystalStyle)st);
        Material passMat = ProceduralMesh_GetPassthroughMaterial(mat.shader);

        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, true);
        rlSetCullFace(RL_CULL_FACE_BACK);
        CrystalMaterial_Begin(mat);
        CrystalMaterial_SetGrowProgress(mat, 1.0f);
        int zero = 0;
        float backZero = 0.0f;
        if (s_crystalHasSceneLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalHasSceneLoc[st], &zero, SHADER_UNIFORM_INT);
        if (s_crystalBackFaceLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalBackFaceLoc[st], &backZero, SHADER_UNIFORM_FLOAT);
        VC_Crystal_DrawStyleMeshes(st, tmpl, passMat);
        CrystalMaterial_End();
        VFXRender_EndDraw(&scope);
    }
}

void VC_Crystal_DrawRefraction(Camera3D camera) {
    (void)camera;
    if (VC_Crystal_GetActiveCount() <= 0 && VC_CrystalShatter_GetActiveCount() <= 0) return;

    Texture2D sceneTex = SceneTargets_GetSceneSnapshotTexture();
    Mesh tmpl = VC_Crystal_GetTemplateMesh();

    for (int st = 0; st < VFX_CRYSTAL_STYLE_COUNT; ++st) {
        if (!VC_Crystal_IsStyleDielectric(st) || !VC_Crystal_HasStyleActive(st)) continue;

        CrystalMaterial mat = VC_Crystal_GetMaterial((VFX_CrystalStyle)st);
        Material passMat = ProceduralMesh_GetPassthroughMaterial(mat.shader);

        /* Pass 1: Rear internal facets (no depth write) */
        VFXRenderScope backScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlSetCullFace(RL_CULL_FACE_FRONT);
        CrystalMaterial_Begin(mat);
        CrystalMaterial_SetGrowProgress(mat, 1.0f);
        int zero = 0;
        float backOne = 1.0f;
        if (s_crystalHasSceneLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalHasSceneLoc[st], &zero, SHADER_UNIFORM_INT);
        if (s_crystalBackFaceLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalBackFaceLoc[st], &backOne, SHADER_UNIFORM_FLOAT);
        VC_Crystal_DrawStyleMeshes(st, tmpl, passMat);
        CrystalMaterial_End();
        rlSetCullFace(RL_CULL_FACE_BACK);
        VFXRender_EndDraw(&backScope);

        /* Pass 2: Front refractive facets sampling clean post-3D scene */
        VFXRenderScope frontScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, true);
        rlSetCullFace(RL_CULL_FACE_BACK);
        CrystalMaterial_Begin(mat);
        CrystalMaterial_SetGrowProgress(mat, 1.0f);
        int hasScene = (sceneTex.id != 0) ? 1 : 0;
        float backZero = 0.0f;
        if (s_crystalHasSceneLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalHasSceneLoc[st], &hasScene, SHADER_UNIFORM_INT);
        if (s_crystalBackFaceLoc[st] >= 0) SetShaderValue(mat.shader, s_crystalBackFaceLoc[st], &backZero, SHADER_UNIFORM_FLOAT);
        if (hasScene && s_crystalSceneLoc[st] >= 0) SetShaderValueTexture(mat.shader, s_crystalSceneLoc[st], sceneTex);
        VC_Crystal_DrawStyleMeshes(st, tmpl, passMat);
        CrystalMaterial_End();
        VFXRender_EndDraw(&frontScope);
    }
}

void VC_Crystal_DrawShadowPass(void) {
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0) return;

    Mesh tmpl = VC_Crystal_GetTemplateMesh();
    Material passMat = ProceduralMesh_GetPassthroughMaterial(depthShader);

    for (int i = 0; i < VC_CRYSTAL_POOL_SIZE; ++i) {
        const CrystalPrimarySlot *slot = &s_crystalPool[i];
        if (!slot->active || !slot->castShadow) continue;
        float grow = VC_Crystal_EvalGrow(slot);
        if (grow <= 0.02f) continue;
        Matrix m = VC_Crystal_BuildMatrix(slot, grow);
        DrawMesh(tmpl, passMat, m);
    }
}
