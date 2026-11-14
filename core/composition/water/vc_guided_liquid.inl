// vc_guided_liquid.inl — Generalized Analytic Guided Liquid System
// Included once by core/composition/water/water.inl
//
// Design principles:
//   1. Single-source O(N) analytic evaluation (Halton(2,3,5) seeds + AnalyticFlow)
//      with zero per-frame allocations and zero CPU PBD grid overhead.
//   2. Backend parity: SSF, Metaball 3D, and Hybrid consume the EXACT SAME
//      evaluated particle positions and radii (GuidedLiquid_EvaluateSamples).
//   3. Auto-sizing: unset particleCount / dropletRadius derive cohesive defaults
//      from emissionRadius so adjacent droplets overlap and fuse cleanly.
//   4. Unified shadows: real directional shadow-map spheres in
//      VC_GuidedLiquid_DrawShadowPass() and smart ground shadows in Draw3D
//      when shadow mapping is disabled.

#define GUIDED_LIQUID_MAX_INSTANCES 16
#define GUIDED_LIQUID_MAX_SAMPLES   2800
#define GUIDED_LIQUID_MAX_SPLASH    400
#define GUIDED_LIQUID_TOTAL_CAP     (GUIDED_LIQUID_MAX_SAMPLES + GUIDED_LIQUID_MAX_SPLASH)

typedef struct {
    bool active;
    int handle;
    VFX_GuidedLiquidConfig cfg;
    float age;
    float travelTime;
    float impactTime;
    float totalDuration;
    bool impactTriggered;
    float lastRippleAge;
    LiquidDesc material;
} GuidedLiquidInstance;

static GuidedLiquidInstance s_glInstances[GUIDED_LIQUID_MAX_INSTANCES];
static int s_glNextHandle = 1;
static VFX_GuidedLiquidConfig s_glLiveConfig;
static bool s_glLiveInit = false;
static AnalyticFlowSample s_glEvalBuf[GUIDED_LIQUID_TOTAL_CAP];

// ── Enum Name Tables ──────────────────────────────────────────────────────────

static const char *const s_glPresetNames[VFX_LIQUID_PRESET_COUNT] = {
    "Custom",
    "Water Orb",
    "Torrent Jet",
    "Vortex Dragon",
    "Orbital Deluge",
    "Spring Gush",
    "Surging Tide",
    "Molten Geyser",
    "Mercurial Orb",
    "Whip Lash",
    "Rain Lance",
    "Tidal Crescent"
};

static const char *const s_glSpawnNames[VFX_LIQUID_SPAWN_COUNT] = {
    "Burst Ejection",
    "Stream Jet",
    "Surround Ring",
    "Orbital Converge",
    "Ground Gush",
    "Cone Spray"
};

static const char *const s_glShapeNames[VFX_LIQUID_SHAPE_COUNT] = {
    "Volume Sphere",
    "Toroidal Ring",
    "Cylinder Column",
    "Double Helix",
    "Planar Disc"
};

static const char *const s_glTrajNames[VFX_LIQUID_TRAJ_COUNT] = {
    "Parabolic Arc",
    "Guided Homing",
    "Spiral Corkscrew",
    "High Catapult",
    "Jet Stream",
    "Serpentine Wave"
};

static const char *const s_glImpactNames[VFX_LIQUID_IMPACT_COUNT] = {
    "Splash Crown",
    "Expanding Puddle",
    "Vortex Maelstrom",
    "Explosive Burst",
    "Deluge Cascade",
    "Piercing Drill"
};

static const char *const s_glBackendNames[VFX_LIQUID_BACKEND_COUNT] = {
    "Metaball 3D",
    "Screen Space Fluid",
    "Hybrid"
};

static const char *const s_glProfileNames[5] = {
    "Water",
    "Poison",
    "Mud",
    "Lava",
    "Liquid Metal"
};

const char *VFX_GuidedLiquidPreset_Name(VFX_GuidedLiquidPreset preset)
{
    if ((int)preset < 0 || preset >= VFX_LIQUID_PRESET_COUNT) return "Unknown";
    return s_glPresetNames[preset];
}

const char *VFX_LiquidSpawnMode_Name(VFX_LiquidSpawnMode mode)
{
    if ((int)mode < 0 || mode >= VFX_LIQUID_SPAWN_COUNT) return "Unknown";
    return s_glSpawnNames[mode];
}

const char *VFX_LiquidEmissionShape_Name(VFX_LiquidEmissionShape shape)
{
    if ((int)shape < 0 || shape >= VFX_LIQUID_SHAPE_COUNT) return "Unknown";
    return s_glShapeNames[shape];
}

const char *VFX_LiquidTrajectory_Name(VFX_LiquidTrajectoryMode traj)
{
    if ((int)traj < 0 || traj >= VFX_LIQUID_TRAJ_COUNT) return "Unknown";
    return s_glTrajNames[traj];
}

const char *VFX_LiquidImpact_Name(VFX_LiquidImpactMode impact)
{
    if ((int)impact < 0 || impact >= VFX_LIQUID_IMPACT_COUNT) return "Unknown";
    return s_glImpactNames[impact];
}

const char *VFX_LiquidBackend_Name(VFX_LiquidRenderBackend backend)
{
    if ((int)backend < 0 || backend >= VFX_LIQUID_BACKEND_COUNT) return "Unknown";
    return s_glBackendNames[backend];
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static float GL_SmoothStep(float e0, float e1, float x)
{
    float t = Clamp((x - e0) / fmaxf(e1 - e0, 1e-5f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float GL_GroundY(float x, float z, float fallbackY)
{
    float gy = MapManager_GetGroundHeightAt(x, z);
    float baseY = isfinite(gy) ? gy : fallbackY;
    float waterY = 0.0f, waterDepth = 0.0f;
    if (MapManager_GetWaterInfoAt(x, z, &waterY, &waterDepth) && waterDepth > 0.01f) {
        return fmaxf(baseY, waterY);
    }
    return baseY;
}

static void GL_ComputeBasis(Vector3 dir, Vector3 *outFwd, Vector3 *outRight, Vector3 *outUp)
{
    float len = Vector3Length(dir);
    Vector3 fwd = (len > 1e-4f) ? Vector3Scale(dir, 1.0f / len) : (Vector3){0.0f, 0.0f, 1.0f};
    Vector3 refUp = (fabsf(fwd.y) > 0.92f) ? (Vector3){0.0f, 0.0f, 1.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(refUp, fwd));
    Vector3 up = Vector3Normalize(Vector3CrossProduct(fwd, right));
    *outFwd = fwd;
    *outRight = right;
    *outUp = up;
}

static void GL_ResolveCountsAndRadii(const VFX_GuidedLiquidConfig *cfg,
                                      int *outCount,
                                      float *outEmitR,
                                      float *outDropR,
                                      int *outSplashCount)
{
    float emitR = (cfg->emissionRadius > 0.02f) ? Clamp(cfg->emissionRadius, 0.05f, 5.0f) : 0.42f;
    float dropR = (cfg->dropletRadius > 0.005f)
                      ? Clamp(cfg->dropletRadius, 0.025f, 0.35f)
                      : Clamp(emitR * 0.22f, 0.065f, 0.14f);
    int count = (cfg->particleCount > 0)
                    ? (int)Clamp((float)cfg->particleCount, 16.0f, (float)GUIDED_LIQUID_MAX_SAMPLES)
                    : (int)Clamp(roundf(420.0f + emitR * 220.0f), 420.0f, 640.0f);
    int splash = (cfg->splashDropletCount > 0)
                     ? (int)Clamp((float)cfg->splashDropletCount, 0.0f, (float)GUIDED_LIQUID_MAX_SPLASH)
                     : 64;
    *outCount = count;
    *outEmitR = emitR;
    *outDropR = dropR;
    *outSplashCount = splash;
}

static LiquidDesc GL_BuildMaterial(const VFX_GuidedLiquidConfig *cfg)
{
    LiquidDesc mat = LiquidSurface_DielectricDesc(cfg->liquidBodyColor,
                                                  cfg->liquidGlowColor,
                                                  cfg->liquidFoamColor);
    mat.ior = (cfg->ior > 1.0f) ? cfg->ior : 1.333f;
    mat.foam = Clamp(cfg->foamAmount, 0.0f, 1.0f);

    if (cfg->liquidProfile == 3) { // Lava
        mat.liquidClass = LIQUID_CLASS_EMISSIVE;
        mat.emission = 2.2f;
        mat.roughnessScale = 1.4f;
        mat.opacityPerMetre = 6.0f;
    } else if (cfg->liquidProfile == 4) { // Liquid Metal
        mat.liquidClass = LIQUID_CLASS_CONDUCTOR;
        mat.ior = 2.5f;
        mat.roughnessScale = 0.28f;
        mat.opacityPerMetre = 8.0f;
    } else if (cfg->liquidProfile == 2) { // Mud
        mat.liquidClass = LIQUID_CLASS_DIELECTRIC;
        mat.roughnessScale = 1.6f;
        mat.opacityPerMetre = 7.5f;
    } else if (cfg->liquidProfile == 1) { // Poison
        mat.liquidClass = LIQUID_CLASS_DIELECTRIC;
        mat.emission = 0.35f;
        mat.opacityPerMetre = 2.4f;
    } else { // Water
        mat.liquidClass = LIQUID_CLASS_DIELECTRIC;
        mat.roughnessScale = 0.45f;
        mat.opacityPerMetre = 0.85f;
    }
    return mat;
}

static void GL_SetWaterColors(VFX_GuidedLiquidConfig *cfg)
{
    cfg->liquidBodyColor = (Color){32, 152, 148, 255};
    cfg->liquidGlowColor = (Color){36, 189, 173, 255};
    cfg->liquidFoamColor = (Color){168, 232, 224, 255};
}

// ── Default & Preset Configurations ──────────────────────────────────────────

VFX_GuidedLiquidConfig VFX_GuidedLiquid_DefaultConfig(void)
{
    VFX_GuidedLiquidConfig cfg = {0};
    cfg.preset = VFX_LIQUID_PRESET_WATER_ORB;
    cfg.spawnMode = VFX_LIQUID_SPAWN_BURST;
    cfg.emissionShape = VFX_LIQUID_SHAPE_SPHERE;
    cfg.trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
    cfg.impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
    cfg.renderBackend = VFX_LIQUID_BACKEND_SSF;
    cfg.liquidProfile = 0;

    // 0 = auto-derived from emissionRadius in GL_ResolveCountsAndRadii,
    // or explicitly populated by ApplyPreset so the inspector shows clear values.
    cfg.particleCount = 512;
    cfg.emissionRadius = 0.42f;
    cfg.emissionInnerRadius = 0.0f;
    cfg.coneAngleDeg = 20.0f;
    cfg.dropletRadius = 0.095f;
    cfg.blobVariation = 0.24f;
    cfg.emergeDuration = 0.0f;
    cfg.hoverDuration = 0.0f;
    cfg.launchStaggerDelay = 0.0f;

    cfg.source = (Vector3){4.5f, 1.25f, 4.4f};
    cfg.target = (Vector3){10.5f, 0.25f, 4.4f};
    cfg.travelSpeed = 12.0f;
    cfg.arcHeight = 1.6f;
    cfg.arcVariation = 0.18f;
    cfg.guideRadius = 0.40f;
    cfg.pullForceN = 2.8f;
    cfg.forwardForceN = 1.2f;
    cfg.swirlSpeed = 5.5f;
    cfg.turbulenceSpeed = 0.45f;
    cfg.fluidDrag = 0.6f;
    cfg.gravityScale = 1.0f;
    cfg.emitMistTrail = false;
    cfg.emitQiRibbon = false;
    cfg.ribbonWidth = 0.18f;

    cfg.splashDropletCount = 64;
    cfg.splashVelocity = 4.2f;
    cfg.wetRadius = 1.8f;
    cfg.wetDuration = 3.0f;
    cfg.puddleRadius = 1.8f;
    cfg.puddleDuration = 3.0f;
    cfg.impactCrown = true;
    cfg.impactShockRing = false;
    cfg.impactDecal = false;
    cfg.impactMistRadius = 0.8f;
    cfg.impactShake = 0.15f;
    cfg.windBlastRadius = 1.5f;

    GL_SetWaterColors(&cfg);
    cfg.ior = 1.333f;
    cfg.specularPower = 96.0f;
    cfg.foamAmount = 0.42f;
    cfg.threshold = 1.0f;
    cfg.cohesionScale = 1.0f;
    return cfg;
}

VFX_GuidedLiquidConfig *VFX_GuidedLiquid_GetLiveConfig(void)
{
    if (!s_glLiveInit) {
        s_glLiveConfig = VFX_GuidedLiquid_DefaultConfig();
        s_glLiveInit = true;
    }
    return &s_glLiveConfig;
}

void VFX_GuidedLiquid_ApplyPreset(VFX_GuidedLiquidConfig *cfg, VFX_GuidedLiquidPreset preset)
{
    if (!cfg) return;
    VFX_LiquidRenderBackend keepBackend = cfg->renderBackend;
    Vector3 keepSrc = cfg->source;
    Vector3 keepDst = cfg->target;
    *cfg = VFX_GuidedLiquid_DefaultConfig();
    cfg->renderBackend = keepBackend;
    if (Vector3LengthSqr(keepSrc) > 1e-4f) cfg->source = keepSrc;
    if (Vector3LengthSqr(keepDst) > 1e-4f) cfg->target = keepDst;
    cfg->preset = preset;

    switch (preset) {
    case VFX_LIQUID_PRESET_TORRENT_JET:
        cfg->spawnMode = VFX_LIQUID_SPAWN_STREAM;
        cfg->emissionShape = VFX_LIQUID_SHAPE_CYLINDER;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_JET_STREAM;
        cfg->impactMode = VFX_LIQUID_IMPACT_PIERCING_DRILL;
        cfg->particleCount = 560;
        cfg->emissionRadius = 0.24f;
        cfg->dropletRadius = 0.082f;
        cfg->coneAngleDeg = 10.0f;
        cfg->launchStaggerDelay = 0.36f;
        cfg->travelSpeed = 16.5f;
        cfg->arcHeight = 0.25f;
        cfg->swirlSpeed = 8.0f;
        break;
    case VFX_LIQUID_PRESET_VORTEX_DRAGON:
        cfg->spawnMode = VFX_LIQUID_SPAWN_STREAM;
        cfg->emissionShape = VFX_LIQUID_SHAPE_HELIX;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW;
        cfg->impactMode = VFX_LIQUID_IMPACT_VORTEX_MAELSTROM;
        cfg->particleCount = 580;
        cfg->emissionRadius = 0.36f;
        cfg->dropletRadius = 0.095f;
        cfg->launchStaggerDelay = 0.42f;
        cfg->travelSpeed = 13.0f;
        cfg->arcHeight = 1.4f;
        cfg->guideRadius = 0.65f;
        cfg->swirlSpeed = 14.0f;
        break;
    case VFX_LIQUID_PRESET_ORBITAL_DELUGE:
        cfg->spawnMode = VFX_LIQUID_SPAWN_ORBITAL_CONVERGE;
        cfg->emissionShape = VFX_LIQUID_SHAPE_RING;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_LIQUID_IMPACT_DELUGE_CASCADE;
        cfg->particleCount = 560;
        cfg->emissionRadius = 1.15f;
        cfg->emissionInnerRadius = 0.55f;
        cfg->dropletRadius = 0.110f;
        cfg->emergeDuration = 0.45f;
        cfg->hoverDuration = 0.25f;
        cfg->travelSpeed = 10.5f;
        cfg->arcHeight = 3.4f;
        cfg->swirlSpeed = 9.0f;
        break;
    case VFX_LIQUID_PRESET_SPRING_GUSH:
        cfg->spawnMode = VFX_LIQUID_SPAWN_GROUND_GUSH;
        cfg->emissionShape = VFX_LIQUID_SHAPE_DISC;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
        cfg->particleCount = 520;
        cfg->emissionRadius = 0.55f;
        cfg->dropletRadius = 0.098f;
        cfg->emergeDuration = 0.28f;
        cfg->travelSpeed = 11.0f;
        cfg->arcHeight = 2.9f;
        break;
    case VFX_LIQUID_PRESET_SURGING_TIDE:
        cfg->spawnMode = VFX_LIQUID_SPAWN_SURROUND_RING;
        cfg->emissionShape = VFX_LIQUID_SHAPE_DISC;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SERPENTINE_WAVE;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPANDING_PUDDLE;
        cfg->particleCount = 600;
        cfg->emissionRadius = 0.72f;
        cfg->dropletRadius = 0.115f;
        cfg->travelSpeed = 11.5f;
        cfg->arcHeight = 0.45f;
        cfg->wetRadius = 2.6f;
        break;
    case VFX_LIQUID_PRESET_MOLTEN_GEYSER:
        cfg->liquidProfile = 3; // Lava
        cfg->spawnMode = VFX_LIQUID_SPAWN_GROUND_GUSH;
        cfg->emissionShape = VFX_LIQUID_SHAPE_SPHERE;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPANDING_PUDDLE;
        cfg->particleCount = 460;
        cfg->emissionRadius = 0.46f;
        cfg->dropletRadius = 0.105f;
        cfg->travelSpeed = 8.5f;
        cfg->arcHeight = 2.2f;
        cfg->liquidBodyColor = (Color){215, 58, 18, 255};
        cfg->liquidGlowColor = (Color){255, 165, 42, 255};
        cfg->liquidFoamColor = (Color){64, 24, 16, 255};
        cfg->ior = 1.55f;
        cfg->foamAmount = 0.55f;
        break;
    case VFX_LIQUID_PRESET_MERCURIAL_ORB:
        cfg->liquidProfile = 4; // Liquid Metal
        cfg->spawnMode = VFX_LIQUID_SPAWN_ORBITAL_CONVERGE;
        cfg->emissionShape = VFX_LIQUID_SHAPE_SPHERE;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_GUIDED_HOMING;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPLOSIVE_BURST;
        cfg->particleCount = 460;
        cfg->emissionRadius = 0.38f;
        cfg->dropletRadius = 0.090f;
        cfg->emergeDuration = 0.25f;
        cfg->travelSpeed = 14.5f;
        cfg->arcHeight = 1.0f;
        cfg->cohesionScale = 1.35f;
        cfg->liquidBodyColor = (Color){188, 202, 218, 255};
        cfg->liquidGlowColor = (Color){240, 248, 255, 255};
        cfg->liquidFoamColor = (Color){115, 128, 148, 255};
        cfg->ior = 2.5f;
        cfg->foamAmount = 0.15f;
        break;
    case VFX_LIQUID_PRESET_WHIP_LASH:
        cfg->spawnMode = VFX_LIQUID_SPAWN_STREAM;
        cfg->emissionShape = VFX_LIQUID_SHAPE_HELIX;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_SERPENTINE_WAVE;
        cfg->impactMode = VFX_LIQUID_IMPACT_SPLASH_CROWN;
        cfg->particleCount = 540;
        cfg->emissionRadius = 0.28f;
        cfg->dropletRadius = 0.080f;
        cfg->launchStaggerDelay = 0.30f;
        cfg->travelSpeed = 15.5f;
        cfg->arcHeight = 0.9f;
        cfg->guideRadius = 0.52f;
        cfg->swirlSpeed = 11.0f;
        break;
    case VFX_LIQUID_PRESET_RAIN_LANCE:
        cfg->spawnMode = VFX_LIQUID_SPAWN_CONE_SPRAY;
        cfg->emissionShape = VFX_LIQUID_SHAPE_CYLINDER;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_JET_STREAM;
        cfg->impactMode = VFX_LIQUID_IMPACT_PIERCING_DRILL;
        cfg->particleCount = 480;
        cfg->emissionRadius = 0.22f;
        cfg->coneAngleDeg = 8.0f;
        cfg->dropletRadius = 0.075f;
        cfg->travelSpeed = 19.0f;
        cfg->arcHeight = 0.35f;
        break;
    case VFX_LIQUID_PRESET_TIDAL_CRESCENT:
        cfg->spawnMode = VFX_LIQUID_SPAWN_SURROUND_RING;
        cfg->emissionShape = VFX_LIQUID_SHAPE_RING;
        cfg->trajectoryMode = VFX_LIQUID_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_LIQUID_IMPACT_EXPANDING_PUDDLE;
        cfg->particleCount = 560;
        cfg->emissionRadius = 0.85f;
        cfg->emissionInnerRadius = 0.42f;
        cfg->dropletRadius = 0.105f;
        cfg->travelSpeed = 13.0f;
        cfg->arcHeight = 0.85f;
        cfg->wetRadius = 2.4f;
        break;
    case VFX_LIQUID_PRESET_WATER_ORB:
    case VFX_LIQUID_PRESET_CUSTOM:
    default:
        break;
    }
}

VFX_GuidedLiquidConfig VFX_GuidedLiquid_PresetConfig(VFX_GuidedLiquidPreset preset)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_DefaultConfig();
    VFX_GuidedLiquid_ApplyPreset(&cfg, preset);
    return cfg;
}

// ── Inspector Parameters ──────────────────────────────────────────────────────

int VFX_GuidedLiquid_GetParams(VFX_GuidedLiquidConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    #define ADD_ENUM(lbl, grp, ptr, names, cnt) do { \
        if (n < maxParams) { \
            outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_ENUM, (void*)(ptr), 0, (cnt)-1, 0, 0, 0, (names), (cnt)}; \
        } \
    } while (0)
    #define ADD_INT(lbl, grp, ptr, mn, mx) do { \
        if (n < maxParams) { \
            outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_INT, (void*)(ptr), (mn), (mx), 0, 0, 0, NULL, 0}; \
        } \
    } while (0)
    #define ADD_FLOAT(lbl, grp, ptr, mn, mx, st) do { \
        if (n < maxParams) { \
            outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_FLOAT, (void*)(ptr), 0, 0, (mn), (mx), (st), NULL, 0}; \
        } \
    } while (0)
    #define ADD_BOOL(lbl, grp, ptr) do { \
        if (n < maxParams) { \
            outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_BOOL, (void*)(ptr), 0, 1, 0, 0, 0, NULL, 0}; \
        } \
    } while (0)

    ADD_ENUM("Preset", "Preset", &cfg->preset, s_glPresetNames, VFX_LIQUID_PRESET_COUNT);
    ADD_ENUM("Render Backend", "Preset", &cfg->renderBackend, s_glBackendNames, VFX_LIQUID_BACKEND_COUNT);
    ADD_ENUM("Liquid Profile", "Preset", &cfg->liquidProfile, s_glProfileNames, 5);

    ADD_ENUM("Spawn Mode", "Emission", &cfg->spawnMode, s_glSpawnNames, VFX_LIQUID_SPAWN_COUNT);
    ADD_ENUM("Emission Shape", "Emission", &cfg->emissionShape, s_glShapeNames, VFX_LIQUID_SHAPE_COUNT);
    ADD_INT("Droplet Count", "Emission", &cfg->particleCount, 16, GUIDED_LIQUID_MAX_SAMPLES);
    ADD_FLOAT("Emission Radius m", "Emission", &cfg->emissionRadius, 0.08f, 3.0f, 0.05f);
    ADD_FLOAT("Inner Radius m", "Emission", &cfg->emissionInnerRadius, 0.0f, 2.5f, 0.05f);
    ADD_FLOAT("Cone Angle deg", "Emission", &cfg->coneAngleDeg, 5.0f, 90.0f, 2.5f);
    ADD_FLOAT("Droplet Radius m", "Emission", &cfg->dropletRadius, 0.02f, 0.35f, 0.005f);
    ADD_FLOAT("Blob Variation", "Emission", &cfg->blobVariation, 0.0f, 0.8f, 0.05f);
    ADD_FLOAT("Emerge Duration s", "Emission", &cfg->emergeDuration, 0.0f, 2.0f, 0.05f);
    ADD_FLOAT("Hover Duration s", "Emission", &cfg->hoverDuration, 0.0f, 1.5f, 0.05f);
    ADD_FLOAT("Launch Stagger s", "Emission", &cfg->launchStaggerDelay, 0.0f, 1.0f, 0.04f);

    ADD_ENUM("Trajectory", "Flight", &cfg->trajectoryMode, s_glTrajNames, VFX_LIQUID_TRAJ_COUNT);
    ADD_FLOAT("Travel Speed m/s", "Flight", &cfg->travelSpeed, 3.0f, 35.0f, 0.5f);
    ADD_FLOAT("Arc Height m", "Flight", &cfg->arcHeight, 0.0f, 8.0f, 0.15f);
    ADD_FLOAT("Guide Radius m", "Flight", &cfg->guideRadius, 0.05f, 2.5f, 0.05f);
    ADD_FLOAT("Swirl Speed rad/s", "Flight", &cfg->swirlSpeed, 0.0f, 25.0f, 0.5f);
    ADD_FLOAT("Cohesion Scale", "Flight", &cfg->cohesionScale, 0.3f, 2.5f, 0.1f);

    ADD_ENUM("Impact Mode", "Impact", &cfg->impactMode, s_glImpactNames, VFX_LIQUID_IMPACT_COUNT);
    ADD_INT("Splash Count", "Impact", &cfg->splashDropletCount, 0, GUIDED_LIQUID_MAX_SPLASH);
    ADD_FLOAT("Splash Velocity m/s", "Impact", &cfg->splashVelocity, 1.0f, 12.0f, 0.25f);
    ADD_FLOAT("Wet Radius m", "Impact", &cfg->wetRadius, 0.2f, 5.0f, 0.2f);
    ADD_BOOL("Impact Crown", "Impact", &cfg->impactCrown);

    ADD_FLOAT("IOR", "Optics", &cfg->ior, 1.05f, 2.8f, 0.02f);
    ADD_FLOAT("Foam Amount", "Optics", &cfg->foamAmount, 0.0f, 1.0f, 0.05f);
    ADD_FLOAT("Metaball Threshold", "Optics", &cfg->threshold, 0.5f, 1.8f, 0.05f);

    #undef ADD_ENUM
    #undef ADD_INT
    #undef ADD_FLOAT
    #undef ADD_BOOL
    return n;
}

// ── Deterministic O(N) Analytic Trajectory Evaluator ──────────────────────────

static Vector3 GL_ShapeOffset(VFX_LiquidEmissionShape shape,
                              float h0, float h1, float h2,
                              float emitR, float innerR,
                              Vector3 fwd, Vector3 right, Vector3 up)
{
    float ang = h0 * 2.0f * PI;
    float rNorm = sqrtf(Clamp(h1, 0.02f, 1.0f));
    float r = Lerp(Clamp(innerR, 0.0f, emitR * 0.9f), emitR, rNorm);

    switch (shape) {
    case VFX_LIQUID_SHAPE_RING: {
        float ringR = Lerp(fmaxf(innerR, emitR * 0.55f), emitR, h1);
        float thick = (h2 - 0.5f) * emitR * 0.28f;
        return Vector3Add(Vector3Scale(right, cosf(ang) * ringR),
                          Vector3Add(Vector3Scale(up, sinf(ang) * ringR),
                                     Vector3Scale(fwd, thick)));
    }
    case VFX_LIQUID_SHAPE_CYLINDER: {
        float axial = (h2 - 0.5f) * emitR * 1.6f;
        float rad = emitR * 0.55f * sqrtf(h1);
        return Vector3Add(Vector3Scale(fwd, axial),
                          Vector3Add(Vector3Scale(right, cosf(ang) * rad),
                                     Vector3Scale(up, sinf(ang) * rad)));
    }
    case VFX_LIQUID_SHAPE_HELIX: {
        float strand = (h2 > 0.5f) ? PI : 0.0f;
        float phase = h1 * 2.0f * PI + strand;
        float axial = (h1 - 0.5f) * emitR * 1.8f;
        float rad = emitR * 0.55f;
        return Vector3Add(Vector3Scale(fwd, axial),
                          Vector3Add(Vector3Scale(right, cosf(phase) * rad),
                                     Vector3Scale(up, sinf(phase) * rad)));
    }
    case VFX_LIQUID_SHAPE_DISC: {
        return Vector3Add(Vector3Scale(right, cosf(ang) * r),
                          Vector3Scale(fwd, sinf(ang) * r * 0.7f));
    }
    case VFX_LIQUID_SHAPE_SPHERE:
    default: {
        // Pack tightly within 0.65 * emitR so droplets overlap into one cohesive orb
        float z = 2.0f * h1 - 1.0f;
        float xy = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        float rad = emitR * 0.65f * powf(Clamp(h2, 0.05f, 1.0f), 0.38f);
        return Vector3Add(Vector3Scale(right, cosf(ang) * xy * rad),
                          Vector3Add(Vector3Scale(up, z * rad),
                                     Vector3Scale(fwd, sinf(ang) * xy * rad)));
    }
    }
}

static Vector3 GL_SpinePos(const VFX_GuidedLiquidConfig *cfg,
                           float u, float age,
                           Vector3 fwd, Vector3 right, Vector3 up)
{
    u = Clamp(u, 0.0f, 1.0f);
    Vector3 p = Vector3Lerp(cfg->source, cfg->target, u);
    float env = sinf(u * PI);

    switch (cfg->trajectoryMode) {
    case VFX_LIQUID_TRAJ_GUIDED_HOMING: {
        float sCurve = u * u * (3.0f - 2.0f * u);
        p = Vector3Lerp(cfg->source, cfg->target, sCurve);
        p.y += env * cfg->arcHeight;
        p = Vector3Add(p, Vector3Scale(right, sinf(u * PI * 1.5f) * cfg->guideRadius * 0.6f * env));
        break;
    }
    case VFX_LIQUID_TRAJ_SPIRAL_CORKSCREW: {
        float theta = u * 4.0f * PI + age * cfg->swirlSpeed * 0.6f;
        float r = cfg->guideRadius * env;
        p.y += env * cfg->arcHeight * 0.75f;
        p = Vector3Add(p, Vector3Add(Vector3Scale(right, cosf(theta) * r),
                                     Vector3Scale(up, sinf(theta) * r)));
        break;
    }
    case VFX_LIQUID_TRAJ_HIGH_CATAPULT: {
        float peak = fmaxf(cfg->arcHeight, 2.8f);
        p.y += 4.0f * u * (1.0f - u) * peak;
        break;
    }
    case VFX_LIQUID_TRAJ_JET_STREAM: {
        p.y += env * fminf(cfg->arcHeight, 0.35f);
        break;
    }
    case VFX_LIQUID_TRAJ_SERPENTINE_WAVE: {
        float wave = sinf(u * 3.0f * PI - age * 7.5f) * cfg->guideRadius * env;
        float lift = cosf(u * 2.0f * PI - age * 5.0f) * cfg->guideRadius * 0.35f * env;
        p.y += env * cfg->arcHeight * 0.65f;
        p = Vector3Add(p, Vector3Add(Vector3Scale(right, wave), Vector3Scale(up, lift)));
        break;
    }
    case VFX_LIQUID_TRAJ_PARABOLIC_ARC:
    default:
        p.y += env * cfg->arcHeight;
        break;
    }
    return p;
}

static int GuidedLiquid_EvaluateSamples(const GuidedLiquidInstance *inst,
                                        AnalyticFlowSample *outSamples,
                                        int maxSamples,
                                        Vector3 *outCenter,
                                        float *outBodyRadius)
{
    const VFX_GuidedLiquidConfig *cfg = &inst->cfg;
    int count = 0, splashCount = 0;
    float emitR = 0.42f, dropR = 0.135f;
    GL_ResolveCountsAndRadii(cfg, &count, &emitR, &dropR, &splashCount);

    Vector3 dir = Vector3Subtract(cfg->target, cfg->source);
    Vector3 fwd, right, up;
    GL_ComputeBasis(dir, &fwd, &right, &up);

    float emergeT = fmaxf(0.0f, cfg->emergeDuration);
    float hoverT = fmaxf(0.0f, cfg->hoverDuration);
    float launchT = emergeT + hoverT;
    float stagger = cfg->launchStaggerDelay;
    if (cfg->spawnMode == VFX_LIQUID_SPAWN_STREAM && stagger < 0.15f) stagger = 0.32f;

    float cohesion = (cfg->cohesionScale > 0.05f) ? cfg->cohesionScale : 1.0f;
    float cohesionScale = 1.0f / Clamp(cohesion, 0.4f, 2.2f);
    float travelT = fmaxf(inst->travelTime, 0.08f);
    float coneRad = cfg->coneAngleDeg * DEG2RAD;

    Vector3 accumPos = {0};
    int activeCount = 0;
    int written = 0;

    for (int i = 0; i < count && written < maxSamples; ++i) {
        float h0 = AnalyticFlow_HaltonSeed(i, 0);
        float h1 = AnalyticFlow_HaltonSeed(i, 1);
        float h2 = AnalyticFlow_HaltonSeed(i, 2);

        float iDelay = ((float)i / (float)count) * stagger;
        float tLocal = inst->age - iDelay;
        if (tLocal < 0.0f) continue;

        float sizeVar = 1.0f + (h2 - 0.5f) * 2.0f * Clamp(cfg->blobVariation, 0.0f, 0.75f);
        float r = dropR * Clamp(sizeVar, 0.55f, 1.45f);
        Vector3 baseOff = GL_ShapeOffset(cfg->emissionShape, h0, h1, h2,
                                         emitR * cohesionScale, cfg->emissionInnerRadius,
                                         fwd, right, up);

        Vector3 pos = cfg->source;
        float alpha = 1.0f;
        float u = 0.0f;

        if (tLocal < emergeT && emergeT > 1e-3f) {
            float e = GL_SmoothStep(0.0f, emergeT, tLocal);
            if (cfg->spawnMode == VFX_LIQUID_SPAWN_ORBITAL_CONVERGE) {
                float orbitAng = h0 * 2.0f * PI + (1.0f - e) * 4.5f;
                float orbitR = Lerp(emitR * 2.2f, emitR * 0.55f, e);
                pos = Vector3Add(cfg->source,
                                 Vector3Add(Vector3Scale(right, cosf(orbitAng) * orbitR),
                                            Vector3Scale(up, sinf(orbitAng) * orbitR)));
            } else if (cfg->spawnMode == VFX_LIQUID_SPAWN_GROUND_GUSH) {
                float gy = GL_GroundY(cfg->source.x, cfg->source.z, cfg->source.y - 1.0f);
                Vector3 gPos = {cfg->source.x + baseOff.x * 0.5f, gy, cfg->source.z + baseOff.z * 0.5f};
                pos = Vector3Lerp(gPos, Vector3Add(cfg->source, baseOff), e);
            } else if (cfg->spawnMode == VFX_LIQUID_SPAWN_SURROUND_RING) {
                float ang = h0 * 2.0f * PI;
                Vector3 ringP = Vector3Add(cfg->source,
                                           Vector3Add(Vector3Scale(right, cosf(ang) * emitR * 1.4f),
                                                      Vector3Scale(fwd, sinf(ang) * emitR * 1.4f)));
                pos = Vector3Lerp(ringP, Vector3Add(cfg->source, baseOff), e);
            } else {
                pos = Vector3Add(cfg->source, Vector3Scale(baseOff, e));
            }
            r *= Lerp(0.35f, 1.0f, e);
        } else if (tLocal < launchT) {
            float spin = (tLocal - emergeT) * cfg->swirlSpeed * 0.5f;
            float cs = cosf(spin), sn = sinf(spin);
            Vector3 spun = Vector3Add(Vector3Scale(right, Vector3DotProduct(baseOff, right) * cs - Vector3DotProduct(baseOff, up) * sn),
                                      Vector3Add(Vector3Scale(up, Vector3DotProduct(baseOff, right) * sn + Vector3DotProduct(baseOff, up) * cs),
                                                 Vector3Scale(fwd, Vector3DotProduct(baseOff, fwd))));
            pos = Vector3Add(cfg->source, spun);
        } else {
            float flightAge = tLocal - launchT;
            u = flightAge / travelT;
            if (u <= 1.0f) {
                Vector3 spine = GL_SpinePos(cfg, u, inst->age, fwd, right, up);
                float spin = u * cfg->swirlSpeed + inst->age * (2.6f + h2 * 1.4f);
                float cs = cosf(spin), sn = sinf(spin);
                float bx = Vector3DotProduct(baseOff, right);
                float by = Vector3DotProduct(baseOff, up);
                float bz = Vector3DotProduct(baseOff, fwd);
                float breath = 1.0f + 0.08f * sinf(inst->age * 8.0f + h1 * 9.0f);
                float headSwell = (u > 0.78f) ? sinf(((u - 0.78f) / 0.22f) * PI) * 0.28f : 0.0f;
                float squeeze = ((cfg->trajectoryMode == VFX_LIQUID_TRAJ_JET_STREAM)
                                     ? Lerp(0.80f, 0.48f, u)
                                     : (1.0f - 0.16f * sinf(u * PI))) * breath + headSwell;
                if (cfg->spawnMode == VFX_LIQUID_SPAWN_CONE_SPRAY) {
                    squeeze += u * tanf(coneRad) * 1.4f;
                }
                Vector3 off = Vector3Add(Vector3Scale(right, (bx * cs - by * sn) * squeeze),
                                         Vector3Add(Vector3Scale(up, (bx * sn + by * cs) * squeeze),
                                                    Vector3Scale(fwd, bz * (1.0f + 0.45f * u))));
                pos = Vector3Add(spine, off);

                // Subtle tail elongation along flight direction
                if (cfg->arcVariation > 0.01f) {
                    pos.y += (h1 - 0.5f) * cfg->arcVariation * sinf(u * PI) * 0.6f;
                }
            } else {
                // Post-impact phase for primary droplets
                float impAge = flightAge - travelT;
                float fade = 1.0f - GL_SmoothStep(0.35f, 1.35f, impAge);
                if (fade <= 0.02f) continue;

                float gy = GL_GroundY(cfg->target.x, cfg->target.z, cfg->target.y);
                float ang = h0 * 2.0f * PI + impAge * ((cfg->impactMode == VFX_LIQUID_IMPACT_VORTEX_MAELSTROM) ? 6.5f : 0.8f);
                float wetR = fmaxf(cfg->wetRadius, 1.0f);

                if (cfg->impactMode == VFX_LIQUID_IMPACT_EXPANDING_PUDDLE) {
                    float rad = Lerp(emitR * 0.4f, wetR * sqrtf(h1), GL_SmoothStep(0.0f, 0.8f, impAge));
                    pos = (Vector3){cfg->target.x + cosf(ang) * rad, gy + r * 0.45f, cfg->target.z + sinf(ang) * rad};
                    r *= Lerp(1.0f, 1.25f, GL_SmoothStep(0.0f, 0.4f, impAge)) * fade;
                } else if (cfg->impactMode == VFX_LIQUID_IMPACT_VORTEX_MAELSTROM) {
                    float rad = Lerp(emitR * 0.5f, wetR * 0.9f * sqrtf(h1), GL_SmoothStep(0.0f, 0.6f, impAge));
                    float lift = (1.0f - h1) * 0.85f * fade;
                    pos = (Vector3){cfg->target.x + cosf(ang) * rad, gy + r * 0.55f + lift, cfg->target.z + sinf(ang) * rad};
                    r *= fade;
                } else if (cfg->impactMode == VFX_LIQUID_IMPACT_DELUGE_CASCADE) {
                    float rad = wetR * 0.85f * sqrtf(h1);
                    float dropH = fmaxf(0.0f, 2.2f * (1.0f - impAge * 1.6f) + h2 * 0.8f);
                    pos = (Vector3){cfg->target.x + cosf(ang) * rad, gy + r * 0.5f + dropH, cfg->target.z + sinf(ang) * rad};
                    r *= fade;
                } else if (cfg->impactMode == VFX_LIQUID_IMPACT_PIERCING_DRILL) {
                    float rad = emitR * (0.4f + impAge * 1.4f) * h1;
                    pos = (Vector3){cfg->target.x + cosf(ang * 2.0f) * rad, gy + r * 0.5f + (1.0f - impAge) * 0.35f, cfg->target.z + sinf(ang * 2.0f) * rad};
                    r *= fade;
                } else {
                    // Physical ballistic impact splash + exponential ground-friction pool
                    float angJit = ang + (h2 - 0.5f) * 1.1f;
                    float fwdPush = cfg->travelSpeed * 0.22f * (0.3f + 0.7f * h2);
                    float vRad = cfg->splashVelocity * (0.20f + 0.80f * h1);
                    float vx = fwd.x * fwdPush + cosf(angJit) * vRad;
                    float vz = fwd.z * fwdPush + sinf(angJit) * vRad;
                    float vUp = cfg->splashVelocity * ((cfg->impactMode == VFX_LIQUID_IMPACT_EXPLOSIVE_BURST) ? 0.95f : 0.70f) * (0.15f + 0.65f * h2);
                    float g = 9.81f * fmaxf(cfg->gravityScale, 0.25f);
                    float yBallistic = gy + r * 0.42f + vUp * impAge - 0.5f * g * impAge * impAge;
                    float tHit = (2.0f * vUp) / g;

                    if (impAge > tHit && yBallistic <= gy + r * 0.42f) {
                        // Ground-sliding puddle with exponential friction deceleration
                        float poolAge = impAge - tHit;
                        float poolFade = 1.0f - GL_SmoothStep(0.25f, 1.25f, poolAge);
                        if (poolFade <= 0.02f) continue;
                        float slideT = tHit + (1.0f - expf(-3.0f * poolAge)) / 3.0f;
                        pos = (Vector3){cfg->target.x + vx * slideT,
                                        gy + r * 0.35f,
                                        cfg->target.z + vz * slideT};
                        r *= Lerp(1.0f, 1.20f, GL_SmoothStep(0.0f, 0.4f, poolAge)) * poolFade;
                        alpha = poolFade;
                    } else {
                        pos = (Vector3){cfg->target.x + vx * impAge,
                                        yBallistic,
                                        cfg->target.z + vz * impAge};
                        r *= fade;
                        alpha = fade;
                    }
                }
                if (cfg->impactMode != VFX_LIQUID_IMPACT_SPLASH_CROWN &&
                    cfg->impactMode != VFX_LIQUID_IMPACT_EXPLOSIVE_BURST) {
                    alpha = fade;
                }
            }
        }

        // Prevent dipping below ground during flight
        float gy = GL_GroundY(pos.x, pos.z, pos.y - 1.0f);
        if (pos.y < gy + r * 0.42f) pos.y = gy + r * 0.42f;

        outSamples[written++] = (AnalyticFlowSample){
            .position = pos,
            .velocity = fwd,
            .radius = r,
            .alpha = alpha,
            .u = u,
            .active = true
        };
        accumPos = Vector3Add(accumPos, pos);
        activeCount++;
    }

    // Worthington crown splash droplets on impact
    if (cfg->impactCrown && splashCount > 0 && inst->age >= inst->impactTime) {
        float splashAge = inst->age - inst->impactTime;
        float crownScale = fmaxf(emitR * 2.2f, cfg->wetRadius * 0.55f);
        int rem = maxSamples - written;
        if (rem > 0) {
            int crownN = AnalyticFlow_EvaluateCrownSplash(cfg->target, splashAge, crownScale,
                                                          (splashCount < rem) ? splashCount : rem,
                                                          &outSamples[written], rem);
            for (int k = 0; k < crownN; ++k) {
                if (outSamples[written + k].active) {
                    accumPos = Vector3Add(accumPos, outSamples[written + k].position);
                    activeCount++;
                }
            }
            written += crownN;
        }
    }

    if (outCenter) {
        *outCenter = (activeCount > 0)
                         ? Vector3Scale(accumPos, 1.0f / (float)activeCount)
                         : GL_SpinePos(cfg, Clamp((inst->age - launchT) / travelT, 0.0f, 1.0f),
                                       inst->age, fwd, right, up);
    }
    if (outBodyRadius) {
        *outBodyRadius = fmaxf(emitR * 1.4f, dropR * 3.0f);
    }
    return written;
}

// ── Composition Entry Points ──────────────────────────────────────────────────

int VFX_ComposeGuidedLiquid(const VFX_GuidedLiquidConfig *cfg)
{
    VFX_GuidedLiquidConfig useCfg = cfg ? *cfg : VFX_GuidedLiquid_DefaultConfig();
    return VFX_ComposeGuidedLiquidEx(useCfg.source, useCfg.target, &useCfg);
}

int VFX_ComposeGuidedLiquidEx(Vector3 source, Vector3 target, const VFX_GuidedLiquidConfig *cfg)
{
    AnalyticFlow_InitSeeds();

    int slot = -1;
    int oldestSlot = 0;
    float maxAge = -1.0f;
    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        if (!s_glInstances[i].active) {
            slot = i;
            break;
        }
        if (s_glInstances[i].age > maxAge) {
            maxAge = s_glInstances[i].age;
            oldestSlot = i;
        }
    }
    if (slot < 0) slot = oldestSlot;

    VFX_GuidedLiquidConfig c = cfg ? *cfg : VFX_GuidedLiquid_DefaultConfig();
    c.source = source;
    c.target = target;

    float dist = Vector3Distance(source, target);
    float speed = (c.travelSpeed > 0.5f) ? c.travelSpeed : 12.0f;
    float travelTime = fmaxf(dist / speed, 0.12f);
    float stagger = (c.spawnMode == VFX_LIQUID_SPAWN_STREAM && c.launchStaggerDelay < 0.15f)
                        ? 0.32f
                        : fmaxf(0.0f, c.launchStaggerDelay);
    float launchT = fmaxf(0.0f, c.emergeDuration) + fmaxf(0.0f, c.hoverDuration);
    float impactTime = launchT + travelTime;
    float totalDuration = impactTime + stagger + 1.55f;

    GuidedLiquidInstance *inst = &s_glInstances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->handle = s_glNextHandle++;
    inst->cfg = c;
    inst->age = 0.0f;
    inst->travelTime = travelTime;
    inst->impactTime = impactTime;
    inst->totalDuration = totalDuration;
    inst->material = GL_BuildMaterial(&c);

    return inst->handle;
}

int VFX_ComposeGuidedLiquidPreset(Vector3 source, Vector3 target, VFX_GuidedLiquidPreset preset)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_PresetConfig(preset);
    return VFX_ComposeGuidedLiquidEx(source, target, &cfg);
}

int VFX_ComposeGuidedLiquidScaled(Vector3 source, Vector3 target, VFX_GuidedLiquidPreset preset, float scale)
{
    VFX_GuidedLiquidConfig cfg = VFX_GuidedLiquid_PresetConfig(preset);
    float s = Clamp(scale, 0.25f, 4.0f);
    cfg.emissionRadius *= s;
    cfg.emissionInnerRadius *= s;
    cfg.dropletRadius *= s;
    cfg.arcHeight *= s;
    cfg.guideRadius *= s;
    cfg.wetRadius *= s;
    return VFX_ComposeGuidedLiquidEx(source, target, &cfg);
}

void VFX_KillGuidedLiquid(int handle)
{
    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        if (s_glInstances[i].active && s_glInstances[i].handle == handle) {
            s_glInstances[i].active = false;
        }
    }
}

bool VFX_GuidedLiquid_IsAnyActive(void)
{
    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        if (s_glInstances[i].active) return true;
    }
    return false;
}

// ── Lifecycle Hooks (Update / Draw3D / SubmitSurface / ShadowPass) ───────────

void VC_GuidedLiquid_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        GuidedLiquidInstance *inst = &s_glInstances[i];
        if (!inst->active) continue;
        inst->age += dt;

        const VFX_GuidedLiquidConfig *cfg = &inst->cfg;
        float launchT = fmaxf(0.0f, cfg->emergeDuration) + fmaxf(0.0f, cfg->hoverDuration);
        float travelT = fmaxf(inst->travelTime, 0.08f);
        Vector3 dir = Vector3Subtract(cfg->target, cfg->source);
        Vector3 fwd, right, up;
        GL_ComputeBasis(dir, &fwd, &right, &up);

        // Flight wake interaction with river/stream SWE water surface
        if (inst->age >= launchT && inst->age < inst->impactTime) {
            float u = Clamp((inst->age - launchT) / travelT, 0.0f, 1.0f);
            Vector3 spine = GL_SpinePos(cfg, u, inst->age, fwd, right, up);
            float waterY = 0.0f, waterDepth = 0.0f;
            float emitR = (cfg->emissionRadius > 0.02f) ? cfg->emissionRadius : 0.42f;
            if (MapManager_GetWaterInfoAt(spine.x, spine.z, &waterY, &waterDepth) &&
                waterDepth > 0.01f && spine.y - emitR <= waterY + 0.65f) {
                Vector3 vel = Vector3Scale(fwd, fmaxf(cfg->travelSpeed, 4.0f));
                MapManager_SetWaterInteractor((Vector3){spine.x, waterY, spine.z},
                                              vel, fmaxf(emitR * 1.6f, 0.55f));
                if (inst->age - inst->lastRippleAge >= 0.14f) {
                    inst->lastRippleAge = inst->age;
                    MapManager_AddWaterRipple((Vector3){spine.x, waterY, spine.z}, 2.4f, 0.55f);
                }
            }
        }

        // One-shot impact: ground wetting/absorption + river SWE shockwave
        if (!inst->impactTriggered && inst->age >= inst->impactTime) {
            inst->impactTriggered = true;
            float emitR = (cfg->emissionRadius > 0.02f) ? cfg->emissionRadius : 0.42f;
            float waterY = 0.0f, waterDepth = 0.0f;
            bool hitWater = MapManager_GetWaterInfoAt(cfg->target.x, cfg->target.z, &waterY, &waterDepth) &&
                            waterDepth > 0.01f;

            if (hitWater) {
                MapManager_AddWaterRipple((Vector3){cfg->target.x, waterY, cfg->target.z},
                                          fmaxf(cfg->wetRadius * 2.4f, 4.5f), 1.45f);
                Vector3 plungeVel = {fwd.x * 2.5f, -fmaxf(cfg->travelSpeed, 6.0f) * 0.65f, fwd.z * 2.5f};
                MapManager_SetWaterInteractor((Vector3){cfg->target.x, waterY, cfg->target.z},
                                              plungeVel, fmaxf(emitR * 2.2f, 0.95f));
            } else if (cfg->wetRadius > 0.05f) {
                Vector3 hitPt = cfg->target;
                Vector3 hitN = {0.0f, 1.0f, 0.0f};
                if (!MapManager_SampleGroundSurfaceAt(cfg->target.x, cfg->target.z, &hitPt, &hitN)) {
                    hitPt.y = GL_GroundY(cfg->target.x, cfg->target.z, cfg->target.y);
                }
                LiquidImpactEvent ev = {0};
                ev.hitPoint = hitPt;
                ev.hitNormal = hitN;
                ev.impulseDirection = fwd;
                ev.initialVelocity = Vector3Scale(fwd, fmaxf(cfg->travelSpeed, 4.0f));
                ev.force01 = 0.85f;
                ev.scale = fmaxf(cfg->wetRadius, 1.0f);
                ev.bodyColor = cfg->liquidBodyColor;
                ev.glowColor = cfg->liquidGlowColor;
                ev.softColor = cfg->liquidFoamColor;
                ev.externalBody = true;
                ev.motionProfile = (LiquidMotionProfile)Clamp((float)cfg->liquidProfile, 0.0f, 4.0f);
                LiquidImpact_SpawnWater(&ev);
            }
        }

        if (inst->age >= inst->totalDuration) {
            inst->active = false;
        }
    }
}

void VC_GuidedLiquid_SubmitSurface(void)
{
    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        const GuidedLiquidInstance *inst = &s_glInstances[i];
        if (!inst->active) continue;

        VFX_LiquidRenderBackend backend = inst->cfg.renderBackend;
        if (backend == VFX_LIQUID_BACKEND_METABALL3D) continue;

        Vector3 center = {0};
        float bodyR = 0.5f;
        int n = GuidedLiquid_EvaluateSamples(inst, s_glEvalBuf, GUIDED_LIQUID_TOTAL_CAP, &center, &bodyR);
        if (n <= 0) continue;

        int pCount = 0, splashCount = 0;
        float emitR = 0.42f, dropR = 0.135f;
        GL_ResolveCountsAndRadii(&inst->cfg, &pCount, &emitR, &dropR, &splashCount);

        LiquidSurface_BindMaterial(&inst->material);
        LiquidSurface_HintBody(center, bodyR);
        LiquidSurface_SetReconstructionRadius(fmaxf(0.14f, dropR * 1.85f));

        for (int k = 0; k < n; ++k) {
            const AnalyticFlowSample *s = &s_glEvalBuf[k];
            if (!s->active || s->radius <= 0.005f) continue;

            // In Hybrid mode, primary body uses Metaball 3D and splash/tail uses SSF
            if (backend == VFX_LIQUID_BACKEND_HYBRID && k < pCount && s->u <= 1.0f) {
                continue;
            }
            LiquidSurface_RegisterParticle(s->position, s->radius);
        }
    }
}

void VC_GuidedLiquid_Draw3D(Camera3D cam)
{
    (void)cam;
    bool drawSmartShadows = !EnvShadow_IsEnabled();

    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        const GuidedLiquidInstance *inst = &s_glInstances[i];
        if (!inst->active) continue;

        VFX_LiquidRenderBackend backend = inst->cfg.renderBackend;
        bool useMetaball = (backend == VFX_LIQUID_BACKEND_METABALL3D || backend == VFX_LIQUID_BACKEND_HYBRID);
        if (!useMetaball && !drawSmartShadows) continue;

        Vector3 center = {0};
        float bodyR = 0.5f;
        int n = GuidedLiquid_EvaluateSamples(inst, s_glEvalBuf, GUIDED_LIQUID_TOTAL_CAP, &center, &bodyR);
        if (n <= 0) continue;

        if (drawSmartShadows) {
            float gy = GL_GroundY(center.x, center.z, center.y - 1.0f);
            float h = fmaxf(0.0f, center.y - gy);
            float fade = 1.0f - Clamp((h - 1.5f) / 2.5f, 0.0f, 1.0f);
            if (fade > 0.05f) {
                float w = bodyR * (0.95f + h * 0.10f) * fade;
                Environment_DrawSmartShadow((Vector3){center.x, gy + 0.02f, center.z},
                                            ENV_SHAPE_SPHERE, w, bodyR * 0.85f * fade);
            }
        }

        if (!useMetaball) continue;

        int pCount = 0, splashCount = 0;
        float emitR = 0.42f, dropR = 0.135f;
        GL_ResolveCountsAndRadii(&inst->cfg, &pCount, &emitR, &dropR, &splashCount);

        float thresh = (inst->cfg.threshold > 0.1f) ? inst->cfg.threshold : 1.0f;
        LiquidMetaball3D_BeginBody(&inst->material, thresh);

        // If total samples exceed LIQUID_METABALL3D_MAX_BLOBS (64), stride evenly so shape & motion match 1:1
        int activeLimit = (backend == VFX_LIQUID_BACKEND_HYBRID) ? pCount : n;
        if (activeLimit > n) activeLimit = n;
        int stride = (activeLimit > LIQUID_METABALL3D_MAX_BLOBS)
                         ? ((activeLimit + LIQUID_METABALL3D_MAX_BLOBS - 1) / LIQUID_METABALL3D_MAX_BLOBS)
                         : 1;
        float rBoost = (stride > 1) ? Clamp(powf((float)stride, 0.34f), 1.0f, 3.0f) : 1.0f;

        int submitted = 0;
        for (int k = 0; k < activeLimit && submitted < LIQUID_METABALL3D_MAX_BLOBS; k += stride) {
            const AnalyticFlowSample *s = &s_glEvalBuf[k];
            if (!s->active || s->radius <= 0.005f) continue;
            if (backend == VFX_LIQUID_BACKEND_HYBRID && s->u > 1.0f) continue;
            LiquidMetaball3D_RegisterBlob(s->position, (s->radius * rBoost) / 0.443f);
            submitted++;
        }

        LiquidMetaball3D_EndBody();
    }
}

void VC_GuidedLiquid_DrawShadowPass(void)
{
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0) return;

    static Mesh s_glShadowSphere = {0};
    static Material s_glShadowMat = {0};
    static bool s_glShadowReady = false;
    if (!s_glShadowReady) {
        s_glShadowSphere = GenMeshSphere(1.0f, 10, 8);
        s_glShadowMat = LoadMaterialDefault();
        s_glShadowReady = true;
    }
    s_glShadowMat.shader = depthShader;

    for (int i = 0; i < GUIDED_LIQUID_MAX_INSTANCES; ++i) {
        const GuidedLiquidInstance *inst = &s_glInstances[i];
        if (!inst->active) continue;

        Vector3 center = {0};
        float bodyR = 0.5f;
        int n = GuidedLiquid_EvaluateSamples(inst, s_glEvalBuf, GUIDED_LIQUID_TOTAL_CAP, &center, &bodyR);
        if (n <= 0) continue;

        // Cast ~18 compact shadow spheres per effect for crisp, fast directional shadows
        int step = (n > 24) ? (n / 18) : 1;
        for (int k = 0; k < n; k += step) {
            const AnalyticFlowSample *s = &s_glEvalBuf[k];
            if (!s->active || s->radius <= 0.01f) continue;
            float r = s->radius * 0.90f;
            Matrix xf = MatrixIdentity();
            xf.m0 = r; xf.m5 = r; xf.m10 = r;
            xf.m12 = s->position.x; xf.m13 = s->position.y; xf.m14 = s->position.z;
            DrawMesh(s_glShadowSphere, s_glShadowMat, xf);
        }
    }
}
