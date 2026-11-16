// vc_tsunami_swe.inl — Shallow Water Equations (SWE) & ATLA Hydro-Manifold VFX
// Included once by core/composition/water/water.inl
//
// Delegates the Virtual-Pipe SWE solver, 20 dynamic hydro-force emitters,
// obstacle bathymetry, and 7 parametric 3D ATLA Waterbending manifold shapes
// to core/liquid/liquid_swe.h.

#include "core/liquid/liquid_swe.h"

static VFX_TsunamiConfig s_tsunamiLiveConfig;
static bool s_tsunamiLiveInit = false;
static VFX_TsunamiPreset s_tsLastLivePreset = VFX_TSUNAMI_PRESET_TIDAL_SURGE;

static const char *const s_tsunamiPatternNames[VFX_TSUNAMI_PATTERN_COUNT] = {
    "Tidal Wave",
    "Radial Deluge",
    "Twin Tidal",
    "ATLA: Tai-Chi Whip",
    "ATLA: Orbital Chakram",
    "ATLA: Water Spout",
    "ATLA: Octopus Form",
    "ATLA: Curling Wave",
    "ATLA: Triple Discs",
    "ATLA: Twin Whips",
    "ATLA: Avatar State"
};

static const char *const s_tsunamiPresetNames[VFX_TSUNAMI_PRESET_COUNT] = {
    "Custom",
    "Tidal Surge",
    "Twin Pincer",
    "Radial Flood",
    "Swift Flash",
    "Abyssal Wall",
    "ATLA Tai-Chi Whip",
    "ATLA Orbital Chakram",
    "ATLA Water Spout",
    "ATLA Octopus Form",
    "ATLA Curling Wave",
    "ATLA Triple Discs",
    "ATLA Twin Whips",
    "ATLA Avatar State"
};

static const char *const s_tsunamiRenderModeNames[3] = {
    "AAA Hydro-Combat",
    "Velocity & Foam",
    "Heightfield"
};

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
    cfg.evaporation = 0.20f;
    cfg.obstacleScale = 0.0f;
    cfg.skillPower = 1.25f;
    cfg.renderMode = 0;
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
    int keepMode = cfg->renderMode;
    *cfg = VFX_Tsunami_DefaultConfig();
    if (Vector3LengthSqr(keepSrc) > 1e-4f) cfg->source = keepSrc;
    if (Vector3LengthSqr(keepDst) > 1e-4f) cfg->target = keepDst;
    cfg->renderMode = keepMode;
    cfg->preset = preset;

    switch (preset) {
    case VFX_TSUNAMI_PRESET_TWIN_PINCER:
        cfg->pattern = VFX_TSUNAMI_TWIN_TIDAL;
        cfg->waveHeight = 2.1f;
        cfg->waveSpeed = 8.8f;
        cfg->width = 13.5f;
        cfg->length = 13.5f;
        cfg->duration = 3.1f;
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
    case VFX_TSUNAMI_PRESET_TAI_CHI_WHIP:
        cfg->pattern = VFX_TSUNAMI_ATLA_TAI_CHI_WHIP;
        cfg->waveHeight = 2.55f;
        cfg->waveSpeed = 10.5f;
        cfg->width = 14.0f;
        cfg->length = 16.0f;
        cfg->duration = 3.35f;
        cfg->skillPower = 1.25f;
        break;
    case VFX_TSUNAMI_PRESET_ORBITAL_CHAKRAM:
        cfg->pattern = VFX_TSUNAMI_ATLA_ORBITAL_CHAKRAM;
        cfg->waveHeight = 2.50f;
        cfg->waveSpeed = 11.0f;
        cfg->width = 14.0f;
        cfg->length = 16.0f;
        cfg->duration = 3.30f;
        cfg->skillPower = 1.25f;
        break;
    case VFX_TSUNAMI_PRESET_WATER_SPOUT:
        cfg->pattern = VFX_TSUNAMI_ATLA_WATER_SPOUT;
        cfg->waveHeight = 2.65f;
        cfg->waveSpeed = 9.0f;
        cfg->width = 14.0f;
        cfg->length = 16.0f;
        cfg->duration = 3.45f;
        cfg->skillPower = 1.30f;
        break;
    case VFX_TSUNAMI_PRESET_OCTOPUS_FORM:
        cfg->pattern = VFX_TSUNAMI_ATLA_OCTOPUS_FORM;
        cfg->waveHeight = 2.60f;
        cfg->waveSpeed = 9.5f;
        cfg->width = 15.0f;
        cfg->length = 15.0f;
        cfg->duration = 3.40f;
        cfg->skillPower = 1.35f;
        break;
    case VFX_TSUNAMI_PRESET_CURLING_WAVE:
        cfg->pattern = VFX_TSUNAMI_ATLA_CURLING_WAVE;
        cfg->waveHeight = 2.75f;
        cfg->waveSpeed = 9.5f;
        cfg->width = 15.5f;
        cfg->length = 16.5f;
        cfg->duration = 3.25f;
        cfg->skillPower = 1.35f;
        break;
    case VFX_TSUNAMI_PRESET_TRIPLE_DISCS:
        cfg->pattern = VFX_TSUNAMI_ATLA_TRIPLE_DISCS;
        cfg->waveHeight = 2.45f;
        cfg->waveSpeed = 11.5f;
        cfg->width = 16.0f;
        cfg->length = 16.5f;
        cfg->duration = 3.60f;
        cfg->skillPower = 1.25f;
        break;
    case VFX_TSUNAMI_PRESET_TWIN_WHIPS:
        cfg->pattern = VFX_TSUNAMI_ATLA_TWIN_WHIPS;
        cfg->waveHeight = 2.20f;
        cfg->waveSpeed = 10.0f;
        cfg->width = 15.5f;
        cfg->length = 16.0f;
        cfg->duration = 3.55f;
        cfg->skillPower = 1.35f;
        break;
    case VFX_TSUNAMI_PRESET_AVATAR_STATE:
        cfg->pattern = VFX_TSUNAMI_ATLA_AVATAR_STATE;
        cfg->waveHeight = 2.85f;
        cfg->waveSpeed = 11.0f;
        cfg->width = 16.5f;
        cfg->length = 17.0f;
        cfg->duration = 3.80f;
        cfg->skillPower = 1.45f;
        cfg->deepColor = (Color){14, 96, 138, 242};
        cfg->crestColor = (Color){44, 206, 224, 236};
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
    T_ENUM("Render Mode", "Optics", &cfg->renderMode, s_tsunamiRenderModeNames, 3);
    T_FLOAT("Wave Height m", "SWE", &cfg->waveHeight, 0.8f, 4.5f, 0.15f);
    T_FLOAT("Wave Speed m/s", "SWE", &cfg->waveSpeed, 3.0f, 16.0f, 0.4f);
    T_FLOAT("Width m", "SWE", &cfg->width, 4.0f, 24.0f, 0.5f);
    T_FLOAT("Length m", "SWE", &cfg->length, 6.0f, 26.0f, 0.5f);
    T_FLOAT("Duration s", "SWE", &cfg->duration, 1.5f, 6.0f, 0.2f);
    T_FLOAT("Skill Power", "SWE", &cfg->skillPower, 0.5f, 2.0f, 0.1f);
    T_FLOAT("Evaporation", "SWE", &cfg->evaporation, 0.02f, 0.65f, 0.04f);
    T_FLOAT("Obstacle Scale", "SWE", &cfg->obstacleScale, 0.0f, 1.8f, 0.1f);
    T_FLOAT("Curl Intensity", "Optics", &cfg->curlIntensity, 0.0f, 1.5f, 0.1f);
    T_FLOAT("Bed Drag", "SWE", &cfg->drag, 0.1f, 2.5f, 0.1f);
    T_BOOL("Crest Spray", "Optics", &cfg->emitCrestSpray);
    T_BOOL("Wet Ground", "Impact", &cfg->emitWetGround);

    #undef T_ENUM
    #undef T_FLOAT
    #undef T_BOOL
    return n;
}

static LiquidSWEPattern VC_TsunamiToSWEPattern(VFX_TsunamiPattern p)
{
    if ((int)p < 0 || (int)p >= LIQUID_SWE_PATTERN_COUNT) return LIQUID_SWE_PATTERN_TIDAL_WAVE;
    return (LiquidSWEPattern)p;
}

int VFX_ComposeTsunami(const VFX_TsunamiConfig *cfg)
{
    VFX_TsunamiConfig c = cfg ? *cfg : VFX_Tsunami_DefaultConfig();
    return VFX_ComposeTsunamiEx(c.source, c.target, &c);
}

int VFX_ComposeTsunamiEx(Vector3 source, Vector3 target, const VFX_TsunamiConfig *cfg)
{
    VFX_TsunamiConfig c = cfg ? *cfg : VFX_Tsunami_DefaultConfig();
    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = VC_TsunamiToSWEPattern(c.pattern);
    desc.source = source;
    desc.target = target;
    desc.waveHeight = c.waveHeight;
    desc.waveSpeed = c.waveSpeed;
    desc.width = c.width;
    desc.length = c.length;
    desc.duration = c.duration;
    desc.curlIntensity = c.curlIntensity;
    desc.drag = c.drag;
    desc.evaporation = (c.evaporation > 0.0f) ? c.evaporation : 0.20f;
    desc.obstacleScale = c.obstacleScale;
    desc.skillPower = (c.skillPower > 0.05f) ? c.skillPower : 1.25f;
    desc.renderMode = c.renderMode;
    desc.emitCrestSpray = c.emitCrestSpray;
    desc.emitWetGround = c.emitWetGround;
    desc.sampleTerrain = true;
    desc.deepColor = c.deepColor;
    desc.crestColor = c.crestColor;
    desc.foamColor = c.foamColor;
    return LiquidSWE_Spawn(&desc);
}

void VFX_KillTsunami(int handle)
{
    LiquidSWE_Kill(handle);
}

bool VFX_Tsunami_IsAnyActive(void)
{
    return LiquidSWE_IsAnyActive();
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
    LiquidSWE_Update(dt);
}

void VC_Tsunami_SubmitSurface(void)
{
    // SWE tsunami & ATLA hydro-manifolds render as unified continuous meshes via tsunami_swe.fs.
}

void VC_Tsunami_Draw3D(Camera3D cam)
{
    LiquidSWE_Draw3D(cam);
}

void VC_Tsunami_DrawShadowPass(void)
{
    LiquidSWE_DrawShadowPass();
}
