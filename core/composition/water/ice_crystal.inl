// ice_crystal.inl — Generalized Glacial Ice Spikes, Prison Ring, Ice Lance Volley & Snow System
// Included once by core/composition/water/water.inl

#define ICE_CRYSTAL_BURST_MAX 32
#define VC_ICE_MAX_INSTANCES  6
#define VC_ICE_MAX_PILLARS    24

typedef struct {
    bool active;
    Vector3 basePos;
    Vector3 dir;
    float radiusScale;
    float heightScale;
    float tiltRad;
    float yawRad;
    float spawnDelay;
    float age;
    float currentGrow;
    bool isLance;
    Vector3 lanceTarget;
    bool erupted;
    bool shattered;
} IcePillarNode;

typedef struct {
    bool active;
    int handle;
    VFX_IceCrystalConfig cfg;
    float age;
    float totalDuration;
    Vector3 fwd;
    Vector3 right;
    int nodeCount;
    IcePillarNode nodes[VC_ICE_MAX_PILLARS];
} IceCrystalInstance;

static IceCrystalInstance s_iceInstances[VC_ICE_MAX_INSTANCES];
static int s_iceNextHandle = 1;
static VFX_IceCrystalConfig s_iceLiveConfig;
static bool s_iceLiveInit = false;
static VFX_IceCrystalPreset s_iceLastLivePreset = VFX_ICE_PRESET_GLACIAL_SPIRE;

static const char *const s_icePresetNames[VFX_ICE_PRESET_COUNT] = {
    "Glacial Spire",
    "Frost Spike Wave",
    "Ice Prison Ring",
    "Ice Lance Volley",
    "Lotus Frost Nova",
    "Glacial Wall",
    "Snow Avalanche"
};

const char *VFX_IceCrystalPreset_Name(VFX_IceCrystalPreset preset)
{
    if ((int)preset < 0 || preset >= VFX_ICE_PRESET_COUNT) return "Unknown";
    return s_icePresetNames[preset];
}

/* ── Legacy Immediate Helpers (Preserved for existing skill callers) ─────── */

void VFX_ComposeIceCrystal(Vector3 basePos, int seed)
{
    CrystalMaterial iceMat = GetIceCrystalMaterial();
    CrystalDesc desc = GetIceCrystalDesc();

    CrystalMaterial_Begin(iceMat);
    ProceduralMesh_DrawCrystalCluster(basePos, &desc, 3, seed, 1.0f, WHITE);
    CrystalMaterial_End();
}

void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress)
{
    Mesh templateMesh = GetIceCrystalTemplateMesh();
    if (templateMesh.vertexCount <= 0)
        return;
    if (crystalCount > ICE_CRYSTAL_BURST_MAX)
        crystalCount = ICE_CRYSTAL_BURST_MAX;
    if (crystalCount <= 0)
        return;

    CrystalDesc desc = GetIceCrystalDesc();
    Matrix transforms[ICE_CRYSTAL_BURST_MAX];

    unsigned int rng = (unsigned int)seed * 747796405u + 2891336453u;
    for (int i = 0; i < crystalCount; i++)
    {
        rng = rng * 1664525u + 1013904223u;
        float r01 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r02 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r03 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r04 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r05 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;

        float angle = r01 * 2.0f * PI;
        float dist = r02 * desc.radius * 0.9f;
        float yOffset = -desc.height * 0.15f * r03;
        Vector3 pos = {
            center.x + cosf(angle) * dist,
            center.y + yOffset,
            center.z + sinf(angle) * dist};

        float heightScale = 0.4f + r04 * 0.8f;
        float radiusScale = 0.4f + r02 * 0.6f;
        float tiltRad = (r05 * 45.0f) * DEG2RAD;

        Matrix scale = MatrixScale(radiusScale, heightScale, radiusScale);
        Matrix rot = MatrixMultiply(MatrixMultiply(MatrixRotateY(-angle), MatrixRotateZ(tiltRad)), MatrixRotateY(angle));
        transforms[i] = MatrixMultiply(MatrixMultiply(scale, rot), MatrixTranslate(pos.x, pos.y, pos.z));
    }

    CrystalMaterialInstanced iceMatI = GetIceCrystalMaterialInstanced();
    CrystalMaterialInstanced_Begin(iceMatI);
    CrystalMaterialInstanced_SetGrowProgress(iceMatI, growProgress);
    Material passthrough = ProceduralMesh_GetPassthroughMaterial(iceMatI.shader);
    DrawMeshInstanced(templateMesh, passthrough, transforms, crystalCount);
    CrystalMaterialInstanced_End();
}

/* ── Generalized Pooled Glacial Ice & Snow System ────────────────────────── */

VFX_IceCrystalConfig VFX_IceCrystal_DefaultConfig(void)
{
    VFX_IceCrystalConfig cfg = {0};
    cfg.preset = VFX_ICE_PRESET_GLACIAL_SPIRE;
    cfg.style = VFX_CRYSTAL_STYLE_ICE;
    cfg.source = (Vector3){1.0f, 0.0f, 4.4f};
    cfg.target = (Vector3){7.5f, 0.0f, 4.4f};
    cfg.crystalCount = 12;
    cfg.height = 2.1f;
    cfg.radius = 0.24f;
    cfg.formationRadius = 1.6f;
    cfg.eruptDuration = 0.28f;
    cfg.holdDuration = 1.75f;
    cfg.shatterDuration = 0.45f;
    cfg.travelSpeed = 16.0f;
    cfg.emitFrostMist = true;
    cfg.emitSnowMotes = true;
    cfg.emitShatterChips = true;
    return cfg;
}

VFX_IceCrystalConfig *VFX_IceCrystal_GetLiveConfig(void)
{
    if (!s_iceLiveInit) {
        s_iceLiveConfig = VFX_IceCrystal_DefaultConfig();
        s_iceLiveInit = true;
    }
    return &s_iceLiveConfig;
}

void VFX_IceCrystal_ApplyPreset(VFX_IceCrystalConfig *cfg, VFX_IceCrystalPreset preset)
{
    if (!cfg) return;
    Vector3 keepSrc = cfg->source;
    Vector3 keepDst = cfg->target;
    *cfg = VFX_IceCrystal_DefaultConfig();
    if (Vector3LengthSqr(keepSrc) > 1e-4f) cfg->source = keepSrc;
    if (Vector3LengthSqr(keepDst) > 1e-4f) cfg->target = keepDst;
    cfg->preset = preset;

    switch (preset) {
    case VFX_ICE_PRESET_FROST_SPIKE_WAVE:
        cfg->crystalCount = 16;
        cfg->height = 1.85f;
        cfg->radius = 0.22f;
        cfg->formationRadius = 0.85f;
        cfg->eruptDuration = 0.22f;
        cfg->holdDuration = 1.35f;
        cfg->travelSpeed = 13.0f;
        break;
    case VFX_ICE_PRESET_ICE_PRISON_RING:
        cfg->crystalCount = 14;
        cfg->height = 2.45f;
        cfg->radius = 0.26f;
        cfg->formationRadius = 2.35f;
        cfg->eruptDuration = 0.26f;
        cfg->holdDuration = 2.30f;
        break;
    case VFX_ICE_PRESET_ICE_LANCE_VOLLEY:
        cfg->crystalCount = 10;
        cfg->height = 1.55f;
        cfg->radius = 0.14f;
        cfg->formationRadius = 1.45f;
        cfg->eruptDuration = 0.45f;
        cfg->holdDuration = 1.25f;
        cfg->travelSpeed = 20.0f;
        break;
    case VFX_ICE_PRESET_LOTUS_FROST_NOVA:
        cfg->crystalCount = 18;
        cfg->height = 1.90f;
        cfg->radius = 0.22f;
        cfg->formationRadius = 2.10f;
        cfg->eruptDuration = 0.25f;
        cfg->holdDuration = 1.65f;
        break;
    case VFX_ICE_PRESET_GLACIAL_WALL:
        cfg->crystalCount = 14;
        cfg->height = 2.65f;
        cfg->radius = 0.30f;
        cfg->formationRadius = 3.20f;
        cfg->eruptDuration = 0.30f;
        cfg->holdDuration = 2.40f;
        break;
    case VFX_ICE_PRESET_SNOW_AVALANCHE:
        cfg->crystalCount = 14;
        cfg->height = 1.45f;
        cfg->radius = 0.20f;
        cfg->formationRadius = 2.20f;
        cfg->eruptDuration = 0.25f;
        cfg->holdDuration = 1.55f;
        cfg->travelSpeed = 11.5f;
        break;
    case VFX_ICE_PRESET_GLACIAL_SPIRE:
    default:
        break;
    }
    s_iceLastLivePreset = cfg->preset;
}

int VFX_IceCrystal_GetParams(VFX_IceCrystalConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    #define I_ENUM(lbl, grp, ptr, names, cnt) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_ENUM, (void*)(ptr), 0, (cnt)-1, 0, 0, 0, (names), (cnt)}; \
    } while (0)
    #define I_INT(lbl, grp, ptr, mn, mx) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_INT, (void*)(ptr), (mn), (mx), 0, 0, 0, NULL, 0}; \
    } while (0)
    #define I_FLOAT(lbl, grp, ptr, mn, mx, st) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_FLOAT, (void*)(ptr), 0, 0, (mn), (mx), (st), NULL, 0}; \
    } while (0)
    #define I_BOOL(lbl, grp, ptr) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_BOOL, (void*)(ptr), 0, 1, 0, 0, 0, NULL, 0}; \
    } while (0)

    I_ENUM("Preset", "Preset", &cfg->preset, s_icePresetNames, VFX_ICE_PRESET_COUNT);
    I_ENUM("Material Style", "Material", &cfg->style, s_crystalStyleNames, VFX_CRYSTAL_STYLE_COUNT);
    I_INT("Crystal Count", "Formation", &cfg->crystalCount, 3, VC_ICE_MAX_PILLARS);
    I_FLOAT("Height m", "Geometry", &cfg->height, 0.6f, 4.0f, 0.15f);
    I_FLOAT("Base Radius m", "Geometry", &cfg->radius, 0.08f, 0.45f, 0.02f);
    I_FLOAT("Spread Radius m", "Formation", &cfg->formationRadius, 0.5f, 5.5f, 0.2f);
    I_FLOAT("Erupt s", "Timing", &cfg->eruptDuration, 0.10f, 1.0f, 0.05f);
    I_FLOAT("Hold s", "Timing", &cfg->holdDuration, 0.4f, 4.5f, 0.2f);
    I_FLOAT("Travel Speed m/s", "Motion", &cfg->travelSpeed, 6.0f, 32.0f, 1.0f);
    I_BOOL("Frost Mist", "Atmosphere", &cfg->emitFrostMist);
    I_BOOL("Snow Motes", "Atmosphere", &cfg->emitSnowMotes);
    I_BOOL("Shatter Chips", "Impact", &cfg->emitShatterChips);

    #undef I_ENUM
    #undef I_INT
    #undef I_FLOAT
    #undef I_BOOL
    return n;
}

static float Ice_GroundY(float x, float z, float fallbackY)
{
    float gy = MapManager_GetGroundHeightAt(x, z);
    return isfinite(gy) ? gy : fallbackY;
}

static float Ice_Hash01(unsigned int *seed)
{
    *seed = (*seed * 1664525u) + 1013904223u;
    return (float)((*seed >> 8) & 0xFFFFu) / 65535.0f;
}

int VFX_ComposeIceCrystalEx(Vector3 source, Vector3 target, const VFX_IceCrystalConfig *cfgIn)
{
    int slot = -1;
    int oldest = 0;
    float maxAge = -1.0f;
    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        if (!s_iceInstances[i].active) { slot = i; break; }
        if (s_iceInstances[i].age > maxAge) { maxAge = s_iceInstances[i].age; oldest = i; }
    }
    if (slot < 0) slot = oldest;

    VFX_IceCrystalConfig cfg = cfgIn ? *cfgIn : VFX_IceCrystal_DefaultConfig();
    cfg.source = source;
    cfg.target = target;
    if (cfg.crystalCount < 3) cfg.crystalCount = 3;
    if (cfg.crystalCount > VC_ICE_MAX_PILLARS) cfg.crystalCount = VC_ICE_MAX_PILLARS;

    IceCrystalInstance *inst = &s_iceInstances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->handle = s_iceNextHandle++;
    if (s_iceNextHandle <= 0) s_iceNextHandle = 1;
    inst->cfg = cfg;
    inst->nodeCount = cfg.crystalCount;

    Vector3 d = {target.x - source.x, 0.0f, target.z - source.z};
    float dist = Vector3Length(d);
    inst->fwd = (dist > 0.05f) ? Vector3Scale(d, 1.0f / dist) : (Vector3){1.0f, 0.0f, 0.0f};
    inst->right = (Vector3){-inst->fwd.z, 0.0f, inst->fwd.x};

    CrystalDesc tmpl = GetIceCrystalDesc();
    float hBaseScale = cfg.height / fmaxf(tmpl.height, 0.1f);
    float rBaseScale = cfg.radius / fmaxf(tmpl.radius, 0.05f);
    unsigned int rng = (unsigned int)(inst->handle * 747796405u + 2891336453u);

    Vector3 freezeCenter = target;
    if (cfg.preset != VFX_ICE_PRESET_ICE_LANCE_VOLLEY) {
        float freezeR = fmaxf(cfg.formationRadius * 1.35f, 1.85f);
        VFX_FreezePatch_Spawn(freezeCenter, freezeR, 0.28f,
                              cfg.eruptDuration + cfg.holdDuration,
                              cfg.shatterDuration + 0.45f,
                              (Color){42, 138, 218, 240},
                              (Color){185, 232, 255, 255});
    }

    float maxDelay = 0.0f;
    for (int i = 0; i < inst->nodeCount; ++i) {
        IcePillarNode *nd = &inst->nodes[i];
        nd->active = true;
        float t = (inst->nodeCount > 1) ? ((float)i / (float)(inst->nodeCount - 1)) : 0.0f;
        float rA = Ice_Hash01(&rng);
        float rB = Ice_Hash01(&rng);
        float rC = Ice_Hash01(&rng);

        if (cfg.preset == VFX_ICE_PRESET_FROST_SPIKE_WAVE || cfg.preset == VFX_ICE_PRESET_SNOW_AVALANCHE) {
            float jitterLat = (rA - 0.5f) * 2.0f * cfg.formationRadius;
            Vector3 p = Vector3Add(Vector3Lerp(source, target, t), Vector3Scale(inst->right, jitterLat));
            p.y = Ice_GroundY(p.x, p.z, target.y) - 0.08f;
            nd->basePos = p;
            nd->heightScale = hBaseScale * (0.55f + 0.65f * t + 0.25f * rB);
            nd->radiusScale = rBaseScale * (0.70f + 0.50f * rC);
            nd->tiltRad = (12.0f + 16.0f * rB) * DEG2RAD;
            nd->yawRad = atan2f(inst->fwd.x, inst->fwd.z) + (rA - 0.5f) * 0.5f;
            nd->spawnDelay = (dist / fmaxf(cfg.travelSpeed, 2.0f)) * t;
        } else if (cfg.preset == VFX_ICE_PRESET_ICE_PRISON_RING) {
            float ang = ((float)i / (float)inst->nodeCount) * 2.0f * PI + (rA - 0.5f) * 0.14f;
            Vector3 p = {
                target.x + cosf(ang) * cfg.formationRadius,
                0.0f,
                target.z + sinf(ang) * cfg.formationRadius
            };
            p.y = Ice_GroundY(p.x, p.z, target.y) - 0.10f;
            nd->basePos = p;
            nd->heightScale = hBaseScale * (0.85f + 0.35f * rB);
            nd->radiusScale = rBaseScale * (0.85f + 0.30f * rC);
            nd->tiltRad = -24.0f * DEG2RAD;
            nd->yawRad = -ang;
            nd->spawnDelay = 0.08f + 0.03f * (float)(i % 4);
        } else if (cfg.preset == VFX_ICE_PRESET_ICE_LANCE_VOLLEY) {
            float arcAng = ((t - 0.5f) * PI * 0.85f);
            Vector3 haloOff = Vector3Add(Vector3Scale(inst->right, sinf(arcAng) * cfg.formationRadius),
                                         (Vector3){0.0f, 1.15f + cosf(arcAng) * cfg.formationRadius * 0.55f, 0.0f});
            nd->basePos = Vector3Add(source, haloOff);
            nd->lanceTarget = Vector3Add(target, (Vector3){(rA - 0.5f) * 0.9f, 0.45f + rB * 0.6f, (rC - 0.5f) * 0.9f});
            nd->isLance = true;
            nd->heightScale = hBaseScale * 0.85f;
            nd->radiusScale = rBaseScale * 0.85f;
            nd->tiltRad = 90.0f * DEG2RAD;
            nd->yawRad = atan2f(nd->lanceTarget.x - nd->basePos.x, nd->lanceTarget.z - nd->basePos.z);
            nd->spawnDelay = 0.06f * (float)i;
        } else if (cfg.preset == VFX_ICE_PRESET_LOTUS_FROST_NOVA) {
            int tier = (i < inst->nodeCount / 2) ? 0 : 1;
            float ringR = (tier == 0) ? (cfg.formationRadius * 0.48f) : cfg.formationRadius;
            float ang = ((float)i / (float)inst->nodeCount) * 4.0f * PI + (tier * 0.35f);
            Vector3 p = {target.x + cosf(ang) * ringR, 0.0f, target.z + sinf(ang) * ringR};
            p.y = Ice_GroundY(p.x, p.z, target.y) - 0.08f;
            nd->basePos = p;
            nd->heightScale = hBaseScale * ((tier == 0) ? 1.05f : 0.78f);
            nd->radiusScale = rBaseScale * (0.80f + 0.35f * rB);
            nd->tiltRad = ((tier == 0) ? 26.0f : 48.0f) * DEG2RAD;
            nd->yawRad = -ang;
            nd->spawnDelay = (tier == 0) ? 0.08f : 0.20f;
        } else if (cfg.preset == VFX_ICE_PRESET_GLACIAL_WALL) {
            float lat = (t - 0.5f) * 2.0f * cfg.formationRadius;
            float arcFwd = -0.35f * (lat * lat) / fmaxf(cfg.formationRadius, 1.0f);
            Vector3 p = Vector3Add(target, Vector3Add(Vector3Scale(inst->right, lat), Vector3Scale(inst->fwd, arcFwd)));
            p.y = Ice_GroundY(p.x, p.z, target.y) - 0.10f;
            nd->basePos = p;
            nd->heightScale = hBaseScale * (1.15f - 0.40f * fabsf(t - 0.5f) * 2.0f + 0.20f * rB);
            nd->radiusScale = rBaseScale * (0.95f + 0.30f * rC);
            nd->tiltRad = (8.0f + 12.0f * (rA - 0.5f)) * DEG2RAD;
            nd->yawRad = atan2f(inst->fwd.x, inst->fwd.z) + (rB - 0.5f) * 0.4f;
            nd->spawnDelay = 0.08f + 0.025f * fabsf((float)i - (float)inst->nodeCount * 0.5f);
        } else {
            /* GLACIAL_SPIRE: 3-Tier Blooming Glacial Rosette sharing a central marrow core */
            int innerCount = (inst->nodeCount > 6) ? 5 : (inst->nodeCount - 1);
            float ang = 0.0f;
            float rad = 0.0f;
            float tiltDeg = 3.0f;
            if (i == 0) {
                /* Tier 0: Thick central hero obelisk */
                rad = 0.0f;
                nd->heightScale = hBaseScale * 1.40f;
                nd->radiusScale = rBaseScale * 1.55f;
                tiltDeg = 3.0f;
                ang = rA * 2.0f * PI;
                nd->spawnDelay = 0.10f;
            } else if (i <= innerCount) {
                /* Tier 1: Inner guard ring tightly rooted around hero spire */
                float frac = (float)(i - 1) / (float)innerCount;
                ang = frac * 2.0f * PI + (rA - 0.5f) * 0.25f;
                rad = (0.14f + 0.14f * rB) * cfg.formationRadius;
                nd->heightScale = hBaseScale * (0.82f + 0.18f * rC);
                nd->radiusScale = rBaseScale * (0.95f + 0.25f * rB);
                tiltDeg = 18.0f + 8.0f * rB;
                nd->spawnDelay = 0.13f + 0.05f * rC;
            } else {
                /* Tier 2: Outer blooming lotus crown */
                int outerIdx = i - innerCount - 1;
                int outerCount = inst->nodeCount - innerCount - 1;
                float frac = (outerCount > 0) ? ((float)outerIdx / (float)outerCount) : 0.0f;
                ang = frac * 2.0f * PI + 0.45f + (rA - 0.5f) * 0.30f;
                rad = (0.28f + 0.22f * rB) * cfg.formationRadius;
                nd->heightScale = hBaseScale * (0.48f + 0.22f * rC);
                nd->radiusScale = rBaseScale * (0.72f + 0.28f * rB);
                tiltDeg = 36.0f + 16.0f * rB;
                nd->spawnDelay = 0.17f + 0.07f * rC;
            }
            Vector3 p = {target.x + cosf(ang) * rad, 0.0f, target.z + sinf(ang) * rad};
            p.y = Ice_GroundY(p.x, p.z, target.y) - 0.04f;
            nd->basePos = p;
            nd->tiltRad = tiltDeg * DEG2RAD;
            nd->yawRad = ang;
        }

        if (nd->spawnDelay > maxDelay) maxDelay = nd->spawnDelay;

        if (!nd->isLance) {
            float st = sinf(nd->tiltRad);
            float ct = cosf(nd->tiltRad);
            Vector3 outDir = (cfg.preset == VFX_ICE_PRESET_GLACIAL_SPIRE)
                ? (Vector3){ cosf(nd->yawRad) * st, ct, sinf(nd->yawRad) * st }
                : Vector3Transform((Vector3){0.0f, 1.0f, 0.0f}, MatrixMultiply(MatrixRotateX(nd->tiltRad), MatrixRotateY(nd->yawRad)));
            VFX_CrystalDesc cDesc = VFX_Crystal_DefaultDesc(cfg.style);
            cDesc.pos          = nd->basePos;
            cDesc.dir          = outDir;
            cDesc.height       = nd->heightScale * tmpl.height;
            cDesc.radius       = nd->radiusScale * tmpl.radius;
            cDesc.eruptTime    = cfg.eruptDuration;
            cDesc.holdTime     = cfg.holdDuration;
            cDesc.fadeTime     = cfg.shatterDuration;
            cDesc.delay        = nd->spawnDelay;
            cDesc.rollDeg      = rC * 360.0f;
            cDesc.castShadow   = true;
            cDesc.shatterOnEnd = cfg.emitShatterChips;
            VFX_Crystal_Spawn(&cDesc);
            nd->active = false;
        }
    }

    inst->totalDuration = maxDelay + cfg.eruptDuration + cfg.holdDuration + cfg.shatterDuration + 0.35f;

    /* Initial mist/snow atmosphere — subtle low-lying katabatic vapor so it never obscures the freeze crust */
    if (cfg.emitFrostMist) {
        VFX_MistVeilConfig mCfg = VFX_MistVeil_DefaultConfig();
        VFX_MistVeil_ApplyPreset(&mCfg,
            (cfg.preset == VFX_ICE_PRESET_SNOW_AVALANCHE) ? VFX_MIST_PRESET_FROST_BREATH : VFX_MIST_PRESET_CRYO_AURA);
        mCfg.radius = fmaxf(cfg.formationRadius * 1.15f, 2.0f);
        mCfg.duration = inst->totalDuration * 0.65f;
        mCfg.density *= 0.35f;
        mCfg.volumetricFog = false;
        Vector3 emitCenter = (cfg.preset == VFX_ICE_PRESET_LOTUS_FROST_NOVA || cfg.preset == VFX_ICE_PRESET_ICE_LANCE_VOLLEY)
                                 ? source : target;
        VFX_ComposeMistVeilConfig(emitCenter, target, &mCfg);
    }

    return inst->handle;
}

void VFX_KillIceCrystal(int handle)
{
    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        if (s_iceInstances[i].active && s_iceInstances[i].handle == handle) {
            s_iceInstances[i].active = false;
        }
    }
}

bool VFX_IceCrystal_IsAnyActive(void)
{
    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        if (s_iceInstances[i].active) return true;
    }
    return false;
}

void VC_IceCrystal_Update(float dt)
{
    if (s_iceLiveInit && s_iceLiveConfig.preset != s_iceLastLivePreset) {
        VFX_IceCrystal_ApplyPreset(&s_iceLiveConfig, s_iceLiveConfig.preset);
    }
    if (dt <= 0.0f) return;

    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        IceCrystalInstance *inst = &s_iceInstances[i];
        if (!inst->active) continue;

        inst->age += dt;
        if (inst->age >= inst->totalDuration) {
            inst->active = false;
            continue;
        }

        const VFX_IceCrystalConfig *cfg = &inst->cfg;
        for (int n = 0; n < inst->nodeCount; ++n) {
            IcePillarNode *nd = &inst->nodes[n];
            if (!nd->active) continue;

            float localT = inst->age - nd->spawnDelay;
            if (localT < 0.0f) {
                nd->currentGrow = 0.0f;
                continue;
            }
            nd->age = localT;

            if (!nd->erupted) {
                nd->erupted = true;
                if (cfg->emitSnowMotes) {
                    VFX_CrystalShatter_Spawn(nd->basePos, (Vector3){0.0f, 1.0f, 0.0f},
                                             2.6f, 3, fmaxf(cfg->radius * 0.45f, 0.06f), cfg->style);
                }
            }

            if (nd->isLance) {
                float chargeT = fmaxf(cfg->eruptDuration, 0.1f);
                Vector3 toT = Vector3Subtract(nd->lanceTarget, nd->basePos);
                float len = Vector3Length(toT);
                float flyT = len / fmaxf(cfg->travelSpeed, 4.0f);
                if (localT < chargeT) {
                    nd->currentGrow = Clamp(localT / chargeT, 0.0f, 1.0f);
                } else if (localT < chargeT + flyT) {
                    float u = (localT - chargeT) / fmaxf(flyT, 0.05f);
                    nd->currentGrow = 1.0f;
                    nd->dir = Vector3Lerp(nd->basePos, nd->lanceTarget, u);
                } else {
                    if (!nd->shattered) {
                        nd->shattered = true;
                        if (cfg->emitShatterChips) {
                            VFX_CrystalShatter_Spawn(nd->lanceTarget, (Vector3){0.0f, 1.0f, 0.0f},
                                                     4.5f, 6, fmaxf(cfg->radius * 0.65f, 0.08f), cfg->style);
                        }
                    }
                    nd->active = false;
                }
            } else {
                float eruptT = fmaxf(cfg->eruptDuration, 0.05f);
                float holdEnd = eruptT + cfg->holdDuration;
                float shatT = fmaxf(cfg->shatterDuration, 0.1f);

                if (localT < eruptT) {
                    float u = localT / eruptT;
                    nd->currentGrow = 1.0f - powf(1.0f - u, 3.0f);
                } else if (localT < holdEnd) {
                    nd->currentGrow = 1.0f;
                } else if (localT < holdEnd + shatT) {
                    if (!nd->shattered) {
                        nd->shattered = true;
                        if (cfg->emitShatterChips) {
                            VFX_CrystalShatter_Spawn(nd->basePos, (Vector3){0.0f, 1.0f, 0.0f},
                                                     4.0f, 6, fmaxf(cfg->radius * 0.70f, 0.09f), cfg->style);
                        }
                    }
                    float u = (localT - holdEnd) / shatT;
                    nd->currentGrow = fmaxf(0.0f, 1.0f - u);
                } else {
                    nd->active = false;
                }
            }
        }
    }
}

static int Ice_CollectTransforms(const IceCrystalInstance *inst, Matrix *outTransforms, int maxOut)
{
    int count = 0;
    for (int n = 0; n < inst->nodeCount && count < maxOut; ++n) {
        const IcePillarNode *nd = &inst->nodes[n];
        if (!nd->active || nd->currentGrow <= 0.02f) continue;

        Vector3 pos = nd->isLance && (nd->age > inst->cfg.eruptDuration) ? nd->dir : nd->basePos;
        float grow = nd->currentGrow;
        Matrix scale = MatrixScale(nd->radiusScale * (0.4f + 0.6f * grow),
                                   nd->heightScale * grow,
                                   nd->radiusScale * (0.4f + 0.6f * grow));
        Matrix rot = MatrixMultiply(MatrixRotateX(nd->tiltRad), MatrixRotateY(nd->yawRad));
        outTransforms[count++] = MatrixMultiply(MatrixMultiply(scale, rot), MatrixTranslate(pos.x, pos.y, pos.z));
    }
    return count;
}

void VC_IceCrystal_Draw3D(Camera3D cam)
{
    (void)cam;
    Mesh templateMesh = VC_Crystal_GetTemplateMesh();
    if (templateMesh.vertexCount <= 0) return;

    Matrix transforms[VC_ICE_MAX_PILLARS];

    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        const IceCrystalInstance *inst = &s_iceInstances[i];
        if (!inst->active) continue;

        int cnt = Ice_CollectTransforms(inst, transforms, VC_ICE_MAX_PILLARS);
        if (cnt <= 0) continue;

        CrystalMaterial mat = VC_Crystal_GetMaterial(inst->cfg.style);
        Material passthrough = ProceduralMesh_GetPassthroughMaterial(mat.shader);
        CrystalMaterial_Begin(mat);
        CrystalMaterial_SetGrowProgress(mat, 1.0f);
        for (int k = 0; k < cnt; ++k) {
            DrawMesh(templateMesh, passthrough, transforms[k]);
        }
        CrystalMaterial_End();
    }
}

void VC_IceCrystal_DrawShadowPass(void)
{
    Mesh templateMesh = VC_Crystal_GetTemplateMesh();
    if (templateMesh.vertexCount <= 0) return;
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0) return;

    Material passthrough = ProceduralMesh_GetPassthroughMaterial(depthShader);
    Matrix transforms[VC_ICE_MAX_PILLARS];

    for (int i = 0; i < VC_ICE_MAX_INSTANCES; ++i) {
        const IceCrystalInstance *inst = &s_iceInstances[i];
        if (!inst->active) continue;

        int cnt = Ice_CollectTransforms(inst, transforms, VC_ICE_MAX_PILLARS);
        if (cnt <= 0) continue;

        for (int k = 0; k < cnt; ++k) {
            DrawMesh(templateMesh, passthrough, transforms[k]);
        }
    }
}
