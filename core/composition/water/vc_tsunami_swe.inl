// vc_tsunami_swe.inl — 2D Shallow Water Equations (SWE) Tsunami VFX
// Included once by core/composition/water/water.inl
//
// Features:
//   1. True 2D Shallow Water Equations solver (h, u, v) over terrain bathymetry b(x,z)
//      with positivity-preserving upwind flux & CFL stability (zero malloc).
//   2. Single-batch curling tsunami mesh (rlBegin(RL_TRIANGLES)) with Beer-Lambert
//      depth coloring, overhanging breaking lip, and aerated crest foam.
//   3. SSF breaking-crest spray droplets along the curling lip.
//   4. River/stream SWE coupling (MapManager_SetWaterInteractor / AddWaterRipple)
//      and conformal ground-wetting absorption (LiquidImpact_SpawnWater).

#define TSUNAMI_MAX_INSTANCES 4
#define TSUNAMI_NX            48
#define TSUNAMI_NZ            48
#define TSUNAMI_GRAVITY       9.81f

typedef struct {
    bool active;
    int handle;
    VFX_TsunamiConfig cfg;
    float age;
    Vector3 origin;
    Vector3 fwd;
    Vector3 right;
    float dx;
    float dz;
    float wetTimer;
    float rippleTimer;
    Vector3 peakPos;
    float peakH;
    float h[TSUNAMI_NZ][TSUNAMI_NX];
    float u[TSUNAMI_NZ][TSUNAMI_NX];
    float v[TSUNAMI_NZ][TSUNAMI_NX];
    float b[TSUNAMI_NZ][TSUNAMI_NX];
    float hNext[TSUNAMI_NZ][TSUNAMI_NX];
    float uNext[TSUNAMI_NZ][TSUNAMI_NX];
    float vNext[TSUNAMI_NZ][TSUNAMI_NX];
    Vector3 posGrid[TSUNAMI_NZ][TSUNAMI_NX];
    Vector3 normGrid[TSUNAMI_NZ][TSUNAMI_NX];
    float foamGrid[TSUNAMI_NZ][TSUNAMI_NX];
} TsunamiInstance;

static TsunamiInstance s_tsunamiInstances[TSUNAMI_MAX_INSTANCES];
static int s_tsunamiNextHandle = 1;
static VFX_TsunamiConfig s_tsunamiLiveConfig;
static bool s_tsunamiLiveInit = false;

static Shader s_tsunamiShader = {0};
static int s_tsFadeLoc = -1;
static int s_tsDeepColorLoc = -1;
static int s_tsCrestColorLoc = -1;
static int s_tsFoamColorLoc = -1;

static void Tsunami_InitShaderIfNeeded(void)
{
    if (s_tsunamiShader.id == 0) {
        s_tsunamiShader = ResourceManager_LoadShader("core/shaders/tsunami_swe.vs",
                                                     "core/shaders/tsunami_swe.fs");
        if (s_tsunamiShader.id != 0) {
            s_tsFadeLoc = GetShaderLocation(s_tsunamiShader, "u_fade");
            s_tsDeepColorLoc = GetShaderLocation(s_tsunamiShader, "u_deepColor");
            s_tsCrestColorLoc = GetShaderLocation(s_tsunamiShader, "u_crestColor");
            s_tsFoamColorLoc = GetShaderLocation(s_tsunamiShader, "u_foamColor");
        }
    }
}

static float Tsunami_SmoothStep(float e0, float e1, float x)
{
    float t = Clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static const char *const s_tsunamiPatternNames[VFX_TSUNAMI_PATTERN_COUNT] = {
    "Tidal Wave",
    "Radial Deluge",
    "Twin Tidal"
};

static const char *const s_tsunamiPresetNames[VFX_TSUNAMI_PRESET_COUNT] = {
    "Custom",
    "Tidal Surge",
    "Twin Pincer",
    "Radial Flood",
    "Swift Flash",
    "Abyssal Wall"
};

static VFX_TsunamiPreset s_tsLastLivePreset = VFX_TSUNAMI_PRESET_TIDAL_SURGE;

const char *VFX_TsunamiPattern_Name(VFX_TsunamiPattern pattern)
{
    if ((int)pattern < 0 || pattern >= VFX_TSUNAMI_PATTERN_COUNT) return "Unknown";
    return s_tsunamiPatternNames[pattern];
}

const char *VFX_TsunamiPreset_Name(VFX_TsunamiPreset preset)
{
    if ((int)preset < 0 || preset >= VFX_TSUNAMI_PRESET_COUNT) return "Unknown";
    return s_tsunamiPresetNames[preset];
}

VFX_TsunamiConfig VFX_Tsunami_DefaultConfig(void)
{
    VFX_TsunamiConfig cfg = {0};
    cfg.preset = VFX_TSUNAMI_PRESET_TIDAL_SURGE;
    cfg.pattern = VFX_TSUNAMI_TIDAL_WAVE;
    cfg.source = (Vector3){1.0f, 0.0f, 4.4f};
    cfg.target = (Vector3){11.0f, 0.0f, 4.4f};
    cfg.waveHeight = 2.2f;
    cfg.waveSpeed = 7.6f;
    cfg.width = 11.5f;
    cfg.length = 13.5f;
    cfg.duration = 3.2f;
    cfg.curlIntensity = 0.90f;
    cfg.drag = 0.42f;
    cfg.emitCrestSpray = false;
    cfg.emitWetGround = true;
    cfg.deepColor = (Color){18, 118, 124, 240};
    cfg.crestColor = (Color){36, 189, 173, 232};
    cfg.foamColor = (Color){216, 248, 246, 245};
    return cfg;
}

VFX_TsunamiConfig *VFX_Tsunami_GetLiveConfig(void)
{
    if (!s_tsunamiLiveInit) {
        s_tsunamiLiveConfig = VFX_Tsunami_DefaultConfig();
        s_tsunamiLiveInit = true;
    }
    return &s_tsunamiLiveConfig;
}

void VFX_Tsunami_ApplyPreset(VFX_TsunamiConfig *cfg, VFX_TsunamiPreset preset)
{
    if (!cfg) return;
    Vector3 keepSrc = cfg->source;
    Vector3 keepDst = cfg->target;
    *cfg = VFX_Tsunami_DefaultConfig();
    if (Vector3LengthSqr(keepSrc) > 1e-4f) cfg->source = keepSrc;
    if (Vector3LengthSqr(keepDst) > 1e-4f) cfg->target = keepDst;
    cfg->preset = preset;

    switch (preset) {
    case VFX_TSUNAMI_PRESET_TWIN_PINCER:
        cfg->pattern = VFX_TSUNAMI_TWIN_TIDAL;
        cfg->waveHeight = 1.9f;
        cfg->waveSpeed = 8.2f;
        cfg->width = 13.0f;
        cfg->length = 12.0f;
        cfg->duration = 3.0f;
        cfg->curlIntensity = 0.85f;
        cfg->drag = 0.40f;
        break;
    case VFX_TSUNAMI_PRESET_RADIAL_FLOOD:
        cfg->pattern = VFX_TSUNAMI_RADIAL_DELUGE;
        cfg->waveHeight = 1.8f;
        cfg->waveSpeed = 7.0f;
        cfg->width = 15.0f;
        cfg->length = 15.0f;
        cfg->duration = 3.2f;
        cfg->curlIntensity = 0.75f;
        cfg->drag = 0.48f;
        break;
    case VFX_TSUNAMI_PRESET_SWIFT_FLASH:
        cfg->pattern = VFX_TSUNAMI_TIDAL_WAVE;
        cfg->waveHeight = 1.25f;
        cfg->waveSpeed = 12.0f;
        cfg->width = 7.5f;
        cfg->length = 16.0f;
        cfg->duration = 2.2f;
        cfg->curlIntensity = 0.65f;
        cfg->drag = 0.30f;
        break;
    case VFX_TSUNAMI_PRESET_ABYSSAL_WALL:
        cfg->pattern = VFX_TSUNAMI_TIDAL_WAVE;
        cfg->waveHeight = 3.4f;
        cfg->waveSpeed = 6.2f;
        cfg->width = 14.5f;
        cfg->length = 15.0f;
        cfg->duration = 4.0f;
        cfg->curlIntensity = 1.15f;
        cfg->drag = 0.50f;
        cfg->deepColor = (Color){10, 72, 88, 245};
        cfg->crestColor = (Color){24, 148, 152, 236};
        break;
    case VFX_TSUNAMI_PRESET_TIDAL_SURGE:
    case VFX_TSUNAMI_PRESET_CUSTOM:
    default:
        break;
    }
    s_tsLastLivePreset = cfg->preset;
}

VFX_TsunamiConfig VFX_Tsunami_Preset(VFX_TsunamiPreset preset)
{
    VFX_TsunamiConfig cfg = VFX_Tsunami_DefaultConfig();
    VFX_Tsunami_ApplyPreset(&cfg, preset);
    return cfg;
}

int VFX_Tsunami_GetParams(VFX_TsunamiConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    #define T_ENUM(lbl, grp, ptr, names, cnt) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_ENUM, (void*)(ptr), 0, (cnt)-1, 0, 0, 0, (names), (cnt)}; \
    } while (0)
    #define T_FLOAT(lbl, grp, ptr, mn, mx, st) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_FLOAT, (void*)(ptr), 0, 0, (mn), (mx), (st), NULL, 0}; \
    } while (0)
    #define T_BOOL(lbl, grp, ptr) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_BOOL, (void*)(ptr), 0, 1, 0, 0, 0, NULL, 0}; \
    } while (0)

    T_ENUM("Preset", "Preset", &cfg->preset, s_tsunamiPresetNames, VFX_TSUNAMI_PRESET_COUNT);
    T_ENUM("Pattern", "SWE", &cfg->pattern, s_tsunamiPatternNames, VFX_TSUNAMI_PATTERN_COUNT);
    T_FLOAT("Wave Height m", "SWE", &cfg->waveHeight, 0.8f, 4.5f, 0.15f);
    T_FLOAT("Wave Speed m/s", "SWE", &cfg->waveSpeed, 3.0f, 16.0f, 0.4f);
    T_FLOAT("Width m", "SWE", &cfg->width, 4.0f, 20.0f, 0.5f);
    T_FLOAT("Length m", "SWE", &cfg->length, 6.0f, 24.0f, 0.5f);
    T_FLOAT("Duration s", "SWE", &cfg->duration, 1.5f, 6.0f, 0.2f);
    T_FLOAT("Curl Intensity", "Optics", &cfg->curlIntensity, 0.0f, 1.5f, 0.1f);
    T_FLOAT("Bed Drag", "SWE", &cfg->drag, 0.1f, 2.5f, 0.1f);
    T_BOOL("Crest Spray", "Optics", &cfg->emitCrestSpray);
    T_BOOL("Wet Ground", "Impact", &cfg->emitWetGround);

    #undef T_ENUM
    #undef T_FLOAT
    #undef T_BOOL
    return n;
}

static Vector3 Tsunami_CellWorldXZ(const TsunamiInstance *inst, int ix, int iz)
{
    float lx = ((float)ix / (float)(TSUNAMI_NX - 1) - 0.5f) * inst->cfg.width;
    float lz = ((float)iz / (float)(TSUNAMI_NZ - 1)) * inst->cfg.length;
    return Vector3Add(inst->origin,
                      Vector3Add(Vector3Scale(inst->right, lx), Vector3Scale(inst->fwd, lz)));
}

static void Tsunami_RebuildMeshCache(TsunamiInstance *inst)
{
    float curlGain = Clamp(inst->cfg.curlIntensity, 0.0f, 1.5f);
    Vector3 peakPos = inst->origin;
    float peakH = 0.0f;

    for (int iz = 0; iz < TSUNAMI_NZ; ++iz) {
        for (int ix = 0; ix < TSUNAMI_NX; ++ix) {
            float h = inst->h[iz][ix];
            Vector3 wp = Tsunami_CellWorldXZ(inst, ix, iz);

            // Gradient of water depth for breaking-lip curl and foam
            float hL = inst->h[iz][ix > 0 ? ix - 1 : ix];
            float hR = inst->h[iz][ix < TSUNAMI_NX - 1 ? ix + 1 : ix];
            float hD = inst->h[iz > 0 ? iz - 1 : iz][ix];
            float hU = inst->h[iz < TSUNAMI_NZ - 1 ? iz + 1 : iz][ix];
            float dhdx = (hR - hL) / (2.0f * inst->dx);
            float dhdz = (hU - hD) / (2.0f * inst->dz);

            float u = inst->u[iz][ix];
            float v = inst->v[iz][ix];
            float spd = sqrtf(u * u + v * v);
            float dirDotSlope = (spd > 0.25f) ? -((u * dhdx + v * dhdz) / spd) : 0.0f;
            float breakFactor = Clamp(dirDotSlope * 0.55f, 0.0f, 1.0f) * Clamp(h / 0.45f, 0.0f, 1.0f);

            // Curling overhang lip pushes the upper crest forward and arcs slightly downward
            float curlDist = fminf(curlGain * breakFactor * h * 0.45f, inst->dz * 2.2f);
            float curlDrop = curlDist * 0.25f * breakFactor;
            if (spd > 0.25f && curlDist > 0.001f) {
                float invS = 1.0f / spd;
                Vector3 flowDir = Vector3Add(Vector3Scale(inst->right, u * invS),
                                             Vector3Scale(inst->fwd, v * invS));
                wp = Vector3Add(wp, Vector3Scale(flowDir, curlDist));
            }

            float y = inst->b[iz][ix] + fmaxf(h - curlDrop, 0.0f);
            inst->posGrid[iz][ix] = (Vector3){wp.x, y, wp.z};
            inst->foamGrid[iz][ix] = Clamp(breakFactor * 1.45f, 0.0f, 1.0f);

            if (h > peakH) {
                peakH = h;
                peakPos = inst->posGrid[iz][ix];
            }
        }
    }

    inst->peakH = peakH;
    inst->peakPos = peakPos;

    for (int iz = 0; iz < TSUNAMI_NZ; ++iz) {
        for (int ix = 0; ix < TSUNAMI_NX; ++ix) {
            Vector3 pL = inst->posGrid[iz][ix > 0 ? ix - 1 : ix];
            Vector3 pR = inst->posGrid[iz][ix < TSUNAMI_NX - 1 ? ix + 1 : ix];
            Vector3 pD = inst->posGrid[iz > 0 ? iz - 1 : iz][ix];
            Vector3 pU = inst->posGrid[iz < TSUNAMI_NZ - 1 ? iz + 1 : iz][ix];
            Vector3 tx = Vector3Subtract(pR, pL);
            Vector3 tz = Vector3Subtract(pU, pD);
            Vector3 n = Vector3CrossProduct(tx, tz);
            float len = Vector3Length(n);
            inst->normGrid[iz][ix] = (len > 1e-5f) ? Vector3Scale(n, 1.0f / len) : (Vector3){0.0f, 1.0f, 0.0f};
        }
    }
}

static void Tsunami_StepSWE(TsunamiInstance *inst, float dt)
{
    float dx = inst->dx;
    float dz = inst->dz;
    float maxVel = 0.42f * fminf(dx, dz) / fmaxf(dt, 1e-4f);
    float dragFactor = expf(-fmaxf(inst->cfg.drag, 0.05f) * dt);

    // 1. Update height via positivity-preserving upwind flux conservation
    for (int iz = 0; iz < TSUNAMI_NZ; ++iz) {
        for (int ix = 0; ix < TSUNAMI_NX; ++ix) {
            float hC = inst->h[iz][ix];

            float uR = (ix < TSUNAMI_NX - 1) ? 0.5f * (inst->u[iz][ix] + inst->u[iz][ix + 1]) : 0.0f;
            float uL = (ix > 0) ? 0.5f * (inst->u[iz][ix - 1] + inst->u[iz][ix]) : 0.0f;
            float vU = (iz < TSUNAMI_NZ - 1) ? 0.5f * (inst->v[iz][ix] + inst->v[iz + 1][ix]) : 0.0f;
            float vD = (iz > 0) ? 0.5f * (inst->v[iz - 1][ix] + inst->v[iz][ix]) : 0.0f;

            float hR_up = (uR >= 0.0f) ? hC : (ix < TSUNAMI_NX - 1 ? inst->h[iz][ix + 1] : 0.0f);
            float hL_up = (uL >= 0.0f) ? (ix > 0 ? inst->h[iz][ix - 1] : 0.0f) : hC;
            float hU_up = (vU >= 0.0f) ? hC : (iz < TSUNAMI_NZ - 1 ? inst->h[iz + 1][ix] : 0.0f);
            float hD_up = (vD >= 0.0f) ? (iz > 0 ? inst->h[iz - 1][ix] : 0.0f) : hC;

            float fluxX = (hR_up * uR - hL_up * uL) / dx;
            float fluxZ = (hU_up * vU - hD_up * vD) / dz;
            float hn = hC - dt * (fluxX + fluxZ);
            inst->hNext[iz][ix] = fmaxf(0.0f, hn);
        }
    }

    // 2. Update horizontal momentum from surface slope grad(h + b) + bed drag
    for (int iz = 0; iz < TSUNAMI_NZ; ++iz) {
        for (int ix = 0; ix < TSUNAMI_NX; ++ix) {
            float hC = inst->hNext[iz][ix];
            if (hC <= 0.004f) {
                inst->uNext[iz][ix] = 0.0f;
                inst->vNext[iz][ix] = 0.0f;
                continue;
            }
            int ixL = ix > 0 ? ix - 1 : ix;
            int ixR = ix < TSUNAMI_NX - 1 ? ix + 1 : ix;
            int izD = iz > 0 ? iz - 1 : iz;
            int izU = iz < TSUNAMI_NZ - 1 ? iz + 1 : iz;

            float etaL = inst->hNext[iz][ixL] + inst->b[iz][ixL];
            float etaR = inst->hNext[iz][ixR] + inst->b[iz][ixR];
            float etaD = inst->hNext[izD][ix] + inst->b[izD][ix];
            float etaU = inst->hNext[izU][ix] + inst->b[izU][ix];

            float dEtaDx = (etaR - etaL) / ((ixR - ixL) * dx + 1e-4f);
            float dEtaDz = (etaU - etaD) / ((izU - izD) * dz + 1e-4f);

            float un = (inst->u[iz][ix] - dt * TSUNAMI_GRAVITY * dEtaDx) * dragFactor;
            float vn = (inst->v[iz][ix] - dt * TSUNAMI_GRAVITY * dEtaDz) * dragFactor;

            // Open sponge boundary damping near domain edges prevents rectangular wall pile-ups
            float nx = (float)ix / (float)(TSUNAMI_NX - 1);
            float nz = (float)iz / (float)(TSUNAMI_NZ - 1);
            float spongeX = Tsunami_SmoothStep(0.0f, 0.14f, nx) * Tsunami_SmoothStep(1.0f, 0.86f, nx);
            float spongeZ = Tsunami_SmoothStep(0.0f, 0.08f, nz) * Tsunami_SmoothStep(1.0f, 0.92f, nz);
            float sponge = 0.76f + 0.24f * (spongeX * spongeZ);
            inst->hNext[iz][ix] *= sponge;
            if (inst->hNext[iz][ix] < 0.35f) {
                inst->hNext[iz][ix] *= expf(-1.4f * dt);
            }

            inst->uNext[iz][ix] = Clamp(un * sponge, -maxVel, maxVel);
            inst->vNext[iz][ix] = Clamp(vn * sponge, -maxVel, maxVel);
        }
    }

    memcpy(inst->h, inst->hNext, sizeof(inst->h));
    memcpy(inst->u, inst->uNext, sizeof(inst->u));
    memcpy(inst->v, inst->vNext, sizeof(inst->v));
}

int VFX_ComposeTsunami(const VFX_TsunamiConfig *cfg)
{
    VFX_TsunamiConfig c = cfg ? *cfg : VFX_Tsunami_DefaultConfig();
    return VFX_ComposeTsunamiEx(c.source, c.target, &c);
}

int VFX_ComposeTsunamiEx(Vector3 source, Vector3 target, const VFX_TsunamiConfig *cfg)
{
    int slot = -1;
    int oldest = 0;
    float maxAge = -1.0f;
    for (int i = 0; i < TSUNAMI_MAX_INSTANCES; ++i) {
        if (!s_tsunamiInstances[i].active) {
            slot = i;
            break;
        }
        if (s_tsunamiInstances[i].age > maxAge) {
            maxAge = s_tsunamiInstances[i].age;
            oldest = i;
        }
    }
    if (slot < 0) slot = oldest;

    VFX_TsunamiConfig c = cfg ? *cfg : VFX_Tsunami_DefaultConfig();
    c.source = source;
    c.target = target;
    c.width = Clamp(c.width, 4.0f, 24.0f);
    c.length = Clamp(c.length, 6.0f, 28.0f);
    c.waveHeight = Clamp(c.waveHeight, 0.6f, 5.0f);
    c.waveSpeed = Clamp(c.waveSpeed, 2.5f, 18.0f);
    c.duration = Clamp(c.duration, 1.0f, 8.0f);

    TsunamiInstance *inst = &s_tsunamiInstances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->handle = s_tsunamiNextHandle++;
    inst->cfg = c;
    inst->dx = c.width / (float)(TSUNAMI_NX - 1);
    inst->dz = c.length / (float)(TSUNAMI_NZ - 1);

    Vector3 dirXZ = {target.x - source.x, 0.0f, target.z - source.z};
    float lenXZ = Vector3Length(dirXZ);
    inst->fwd = (lenXZ > 1e-4f) ? Vector3Scale(dirXZ, 1.0f / lenXZ) : (Vector3){1.0f, 0.0f, 0.0f};
    inst->right = (Vector3){-inst->fwd.z, 0.0f, inst->fwd.x};
    inst->origin = (c.pattern == VFX_TSUNAMI_TIDAL_WAVE)
                       ? source
                       : Vector3Subtract(target, Vector3Scale(inst->fwd, c.length * 0.5f));

    // Sample terrain bathymetry & seed initial SWE wave bore
    for (int iz = 0; iz < TSUNAMI_NZ; ++iz) {
        float nz = (float)iz / (float)(TSUNAMI_NZ - 1);
        for (int ix = 0; ix < TSUNAMI_NX; ++ix) {
            float nx = (float)ix / (float)(TSUNAMI_NX - 1);
            Vector3 wp = Tsunami_CellWorldXZ(inst, ix, iz);
            float gy = MapManager_GetGroundHeightAt(wp.x, wp.z);
            float wy = 0.0f, wd = 0.0f;
            if (MapManager_GetWaterInfoAt(wp.x, wp.z, &wy, &wd) && wd > 0.01f) {
                gy = fmaxf(gy, wy);
            }
            inst->b[iz][ix] = isfinite(gy) ? gy : source.y;

            float sideFade = sinf(Clamp(nx, 0.0f, 1.0f) * PI);
            sideFade = powf(fmaxf(sideFade, 0.0f), 1.45f);

            if (c.pattern == VFX_TSUNAMI_RADIAL_DELUGE) {
                float cx = (nx - 0.5f) * c.width;
                float cz = (nz - 0.5f) * c.length;
                float rad = sqrtf(cx * cx + cz * cz);
                float ring = expf(-powf((rad - 1.4f) / 1.1f, 2.0f));
                inst->h[iz][ix] = c.waveHeight * ring;
                if (rad > 0.1f && ring > 0.05f) {
                    inst->u[iz][ix] = (cx / rad) * c.waveSpeed * ring;
                    inst->v[iz][ix] = (cz / rad) * c.waveSpeed * ring;
                }
            } else if (c.pattern == VFX_TSUNAMI_TWIN_TIDAL) {
                float zFade = powf(fmaxf(sinf(nz * PI), 0.0f), 0.75f);
                float leftWall = expf(-powf((nx - 0.22f) * 7.5f, 2.0f));
                float rightWall = expf(-powf((nx - 0.78f) * 7.5f, 2.0f));
                inst->h[iz][ix] = c.waveHeight * (leftWall + rightWall) * zFade;
                inst->u[iz][ix] = (leftWall - rightWall) * c.waveSpeed;
            } else {
                // TIDAL_WAVE: organic crescent-arc bore with tapered side wings
                float sideX = (nx - 0.5f) * 2.0f;
                float arcZ = 0.26f - 0.11f * sideX * sideX + 0.015f * sinf(nx * 13.0f);
                float s = (nz - arcZ) * c.length * 0.90f;
                float sech = 1.0f / coshf(Clamp(s, -6.0f, 6.0f));
                float organic = 1.0f + 0.08f * sinf(nx * 11.0f) + 0.05f * cosf(nx * 19.0f);
                float bore = sech * sech * sideFade * organic;
                inst->h[iz][ix] = c.waveHeight * bore;
                inst->v[iz][ix] = c.waveSpeed * powf(fmaxf(bore, 0.0f), 0.35f);
            }
        }
    }

    Tsunami_RebuildMeshCache(inst);
    return inst->handle;
}

void VFX_KillTsunami(int handle)
{
    for (int i = 0; i < TSUNAMI_MAX_INSTANCES; ++i) {
        if (s_tsunamiInstances[i].active && s_tsunamiInstances[i].handle == handle) {
            s_tsunamiInstances[i].active = false;
        }
    }
}

bool VFX_Tsunami_IsAnyActive(void)
{
    for (int i = 0; i < TSUNAMI_MAX_INSTANCES; ++i) {
        if (s_tsunamiInstances[i].active) return true;
    }
    return false;
}

void VC_Tsunami_Update(float dt)
{
    if (s_tsunamiLiveInit && s_tsunamiLiveConfig.preset != s_tsLastLivePreset) {
        if (s_tsunamiLiveConfig.preset != VFX_TSUNAMI_PRESET_CUSTOM) {
            VFX_Tsunami_ApplyPreset(&s_tsunamiLiveConfig, s_tsunamiLiveConfig.preset);
        } else {
            s_tsLastLivePreset = VFX_TSUNAMI_PRESET_CUSTOM;
        }
    }
    if (dt <= 0.0f) return;
    float stepDt = fminf(dt, 0.033f) * 0.5f;

    for (int i = 0; i < TSUNAMI_MAX_INSTANCES; ++i) {
        TsunamiInstance *inst = &s_tsunamiInstances[i];
        if (!inst->active) continue;

        inst->age += dt;
        if (inst->age >= inst->cfg.duration) {
            inst->active = false;
            continue;
        }

        Tsunami_StepSWE(inst, stepDt);
        Tsunami_StepSWE(inst, stepDt);
        Tsunami_RebuildMeshCache(inst);

        // Couple with river/stream SWE water & spawn ground wetting marks
        if (inst->peakH > 0.12f) {
            float wy = 0.0f, wd = 0.0f;
            if (MapManager_GetWaterInfoAt(inst->peakPos.x, inst->peakPos.z, &wy, &wd) && wd > 0.01f) {
                Vector3 vel = Vector3Scale(inst->fwd, inst->cfg.waveSpeed);
                MapManager_SetWaterInteractor((Vector3){inst->peakPos.x, wy, inst->peakPos.z},
                                              vel, fmaxf(inst->cfg.width * 0.28f, 1.4f));
                inst->rippleTimer += dt;
                if (inst->rippleTimer >= 0.16f) {
                    inst->rippleTimer = 0.0f;
                    MapManager_AddWaterRipple((Vector3){inst->peakPos.x, wy, inst->peakPos.z}, 5.5f, 1.3f);
                }
            } else if (inst->cfg.emitWetGround) {
                inst->wetTimer += dt;
                if (inst->wetTimer >= 0.28f) {
                    inst->wetTimer = 0.0f;
                    Vector3 hitPt = inst->peakPos;
                    Vector3 hitN = {0.0f, 1.0f, 0.0f};
                    MapManager_SampleGroundSurfaceAt(inst->peakPos.x, inst->peakPos.z, &hitPt, &hitN);
                    LiquidImpactEvent ev = {0};
                    ev.hitPoint = hitPt;
                    ev.hitNormal = hitN;
                    ev.impulseDirection = inst->fwd;
                    ev.initialVelocity = Vector3Scale(inst->fwd, inst->cfg.waveSpeed);
                    ev.force01 = 0.90f;
                    ev.scale = fmaxf(inst->cfg.width * 0.25f, 1.8f);
                    ev.bodyColor = inst->cfg.deepColor;
                    ev.glowColor = inst->cfg.crestColor;
                    ev.softColor = inst->cfg.foamColor;
                    ev.externalBody = true;
                    LiquidImpact_SpawnWater(&ev);
                }
            }
        }
    }
}

void VC_Tsunami_SubmitSurface(void)
{
    // SWE tsunami renders as a unified continuous mesh via tsunami_swe.fs;
    // avoid emitting sparse SSF spheres that mismatch the mesh optics.
}

void VC_Tsunami_Draw3D(Camera3D cam)
{
    (void)cam;
    Tsunami_InitShaderIfNeeded();
    if (s_tsunamiShader.id == 0) return;

    bool drawSmartShadow = !EnvShadow_IsEnabled();

    for (int i = 0; i < TSUNAMI_MAX_INSTANCES; ++i) {
        const TsunamiInstance *inst = &s_tsunamiInstances[i];
        if (!inst->active) continue;

        float fade = 1.0f - Clamp((inst->age - inst->cfg.duration * 0.68f) / (inst->cfg.duration * 0.32f), 0.0f, 1.0f);
        if (fade <= 0.01f) continue;

        if (drawSmartShadow && inst->peakH > 0.15f) {
            float gy = MapManager_GetGroundHeightAt(inst->peakPos.x, inst->peakPos.z);
            Environment_DrawSmartShadow((Vector3){inst->peakPos.x, gy + 0.03f, inst->peakPos.z},
                                        ENV_SHAPE_SPHERE, inst->cfg.width * 0.45f * fade, inst->peakH * 0.9f * fade);
        }

        float deepV[4]  = {inst->cfg.deepColor.r / 255.0f, inst->cfg.deepColor.g / 255.0f, inst->cfg.deepColor.b / 255.0f, 1.0f};
        float crestV[4] = {inst->cfg.crestColor.r / 255.0f, inst->cfg.crestColor.g / 255.0f, inst->cfg.crestColor.b / 255.0f, 1.0f};
        float foamV[4]  = {inst->cfg.foamColor.r / 255.0f, inst->cfg.foamColor.g / 255.0f, inst->cfg.foamColor.b / 255.0f, 1.0f};

        VFXRenderScope renderScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDisableBackfaceCulling();
        BeginShaderMode(s_tsunamiShader);
        SkillManager_BeginShader(s_tsunamiShader);

        if (s_tsFadeLoc >= 0) SetShaderValue(s_tsunamiShader, s_tsFadeLoc, &fade, SHADER_UNIFORM_FLOAT);
        if (s_tsDeepColorLoc >= 0) SetShaderValue(s_tsunamiShader, s_tsDeepColorLoc, deepV, SHADER_UNIFORM_VEC4);
        if (s_tsCrestColorLoc >= 0) SetShaderValue(s_tsunamiShader, s_tsCrestColorLoc, crestV, SHADER_UNIFORM_VEC4);
        if (s_tsFoamColorLoc >= 0) SetShaderValue(s_tsunamiShader, s_tsFoamColorLoc, foamV, SHADER_UNIFORM_VEC4);

        rlBegin(RL_TRIANGLES);

        for (int iz = 0; iz < TSUNAMI_NZ - 1; ++iz) {
            float nz0 = (float)iz / (float)(TSUNAMI_NZ - 1);
            float nz1 = (float)(iz + 1) / (float)(TSUNAMI_NZ - 1);
            float fz0 = Tsunami_SmoothStep(0.0f, 0.08f, nz0) * Tsunami_SmoothStep(1.0f, 0.92f, nz0);
            float fz1 = Tsunami_SmoothStep(0.0f, 0.08f, nz1) * Tsunami_SmoothStep(1.0f, 0.92f, nz1);

            for (int ix = 0; ix < TSUNAMI_NX - 1; ++ix) {
                float h00 = inst->h[iz][ix];
                float h10 = inst->h[iz][ix + 1];
                float h01 = inst->h[iz + 1][ix];
                float h11 = inst->h[iz + 1][ix + 1];
                if (h00 < 0.002f && h10 < 0.002f && h01 < 0.002f && h11 < 0.002f) continue;

                float nx0 = (float)ix / (float)(TSUNAMI_NX - 1);
                float nx1 = (float)(ix + 1) / (float)(TSUNAMI_NX - 1);
                float fx0 = Tsunami_SmoothStep(0.0f, 0.14f, nx0) * Tsunami_SmoothStep(1.0f, 0.86f, nx0);
                float fx1 = Tsunami_SmoothStep(0.0f, 0.14f, nx1) * Tsunami_SmoothStep(1.0f, 0.86f, nx1);

                const Vector3 vtx[4] = {
                    inst->posGrid[iz][ix],
                    inst->posGrid[iz][ix + 1],
                    inst->posGrid[iz + 1][ix],
                    inst->posGrid[iz + 1][ix + 1]
                };
                const Vector3 nrm[4] = {
                    inst->normGrid[iz][ix],
                    inst->normGrid[iz][ix + 1],
                    inst->normGrid[iz + 1][ix],
                    inst->normGrid[iz + 1][ix + 1]
                };
                const float hs[4] = {h00, h10, h01, h11};
                const float edgeF[4] = {fx0 * fz0, fx1 * fz0, fx0 * fz1, fx1 * fz1};
                const float fm[4] = {
                    inst->foamGrid[iz][ix],
                    inst->foamGrid[iz][ix + 1],
                    inst->foamGrid[iz + 1][ix],
                    inst->foamGrid[iz + 1][ix + 1]
                };

                float invRefH = 1.0f / fmaxf(inst->cfg.waveHeight * 0.78f, 0.35f);

                // CCW winding from above (+Y): (0,1,2) and (1,3,2)
                const int idx[6] = {0, 1, 2, 1, 3, 2};
                for (int t = 0; t < 6; ++t) {
                    int id = idx[t];
                    float depth01 = Clamp(hs[id] * invRefH, 0.0f, 1.0f) * edgeF[id];
                    rlColor4ub(255, 255, 255, 255);
                    rlTexCoord2f(depth01, fm[id] * edgeF[id]);
                    rlNormal3f(nrm[id].x, nrm[id].y, nrm[id].z);
                    rlVertex3f(vtx[id].x, vtx[id].y, vtx[id].z);
                }
            }
        }

        rlEnd();
        rlDrawRenderBatchActive();
        EndShaderMode();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&renderScope);
    }
}

void VC_Tsunami_DrawShadowPass(void)
{
    // Translucent water should not cast pitch-black opaque depth-map silhouettes.
}
