// ── vc_mist_veil.inl — Generalized Mist, Steam, Frost Vapor & Blizzard System ─
//
// Multi-layered atmospheric & phase-change vapor composition supporting 6 presets:
//   1. River Shroud     (Sương Ẩn Giang Hồ — ground mist + god-rays + dew motes)
//   2. Steam Geyser     (Nhiệt Thủy Hơi Nước — buoyant rising hot steam plume)
//   3. Frost Breath     (Hàn Khí Xung Kích — directional sub-zero frost cone jet)
//   4. Cryo Aura        (Hàn Băng Hộ Thể — katabatic ground-hugging cold mist)
//   5. Blizzard Vortex  (Tuyết Vũ Hàn Phong — swirling snow & freezing fog cyclone)
//   6. Phase Steam Burst(Thủy Hỏa Hóa Vụ — explosive flash-evaporation steam ring)

#include "core/resource_manager.h"
#include "environment/environment_system.h"

static Texture2D s_mistSoftTex = {0};
static Texture2D s_dewTex = {0};
static SkillCurve s_mistGrow = {0};
static SkillCurve s_mistFade = {0};
static SkillCurve s_dewFade = {0};
static bool s_mistVeilReady = false;

static VFX_MistVeilConfig s_mistLiveConfig;
static bool s_mistLiveInit = false;
static VFX_MistVeilPreset s_mistLastLivePreset = VFX_MIST_PRESET_RIVER_SHROUD;

static const char *const s_mistPresetNames[VFX_MIST_PRESET_COUNT] = {
    "River Shroud",
    "Steam Geyser",
    "Frost Breath",
    "Cryo Aura",
    "Blizzard Vortex",
    "Phase Steam Burst"
};

const char *VFX_MistVeilPreset_Name(VFX_MistVeilPreset preset)
{
    if ((int)preset < 0 || preset >= VFX_MIST_PRESET_COUNT) return "Unknown";
    return s_mistPresetNames[preset];
}

static void MistVeil_Init(void)
{
    if (s_mistVeilReady) return;

    const int S = 128;
    Image mistImg = GenImageColor(S, S, BLANK);
    for (int y = 0; y < S; y++) {
        float ny = ((float)y + 0.5f) / (float)S * 2.0f - 1.0f;
        for (int x = 0; x < S; x++) {
            float nx = ((float)x + 0.5f) / (float)S * 2.0f - 1.0f;
            float r = sqrtf(nx * nx + ny * ny);
            if (r >= 1.0f) continue;

            float falloff = (1.0f - r * r);
            falloff = falloff * falloff;

            float angle = atan2f(ny, nx);
            float swirl = 1.0f + 0.20f * sinf(3.0f * angle + 2.5f * r)
                               + 0.12f * cosf(5.0f * angle - 3.2f * r);
            float density = falloff * swirl;
            if (density > 1.0f) density = 1.0f;
            if (density < 0.0f) density = 0.0f;

            unsigned char a = (unsigned char)(density * 255.0f);
            ImageDrawPixel(&mistImg, x, y, (Color){255, 255, 255, a});
        }
    }
    s_mistSoftTex = LoadTextureFromImage(mistImg);
    UnloadImage(mistImg);
    if (s_mistSoftTex.id != 0) {
        SetTextureFilter(s_mistSoftTex, TEXTURE_FILTER_BILINEAR);
    }

    const int D = 32;
    Image dewImg = GenImageColor(D, D, BLANK);
    for (int y = 0; y < D; y++) {
        float v = ((float)y + 0.5f) / (float)D * 2.0f - 1.0f;
        for (int x = 0; x < D; x++) {
            float u = ((float)x + 0.5f) / (float)D * 2.0f - 1.0f;
            float r2 = u * u + v * v;
            if (r2 < 1.0f) {
                float a = expf(-r2 * 4.0f);
                unsigned char val = (unsigned char)(a * 255.0f);
                ImageDrawPixel(&dewImg, x, y, (Color){255, 255, 255, val});
            }
        }
    }
    s_dewTex = LoadTextureFromImage(dewImg);
    UnloadImage(dewImg);
    if (s_dewTex.id != 0) {
        SetTextureFilter(s_dewTex, TEXTURE_FILTER_BILINEAR);
    }

    FloatCurve_AddStop(&s_mistGrow, 0.0f, 0.85f);
    FloatCurve_AddStop(&s_mistGrow, 0.35f, 1.15f);
    FloatCurve_AddStop(&s_mistGrow, 1.0f, 1.42f);

    FloatCurve_AddStop(&s_mistFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_mistFade, 0.18f, 1.0f);
    FloatCurve_AddStop(&s_mistFade, 0.65f, 0.85f);
    FloatCurve_AddStop(&s_mistFade, 1.0f, 0.0f);

    FloatCurve_AddStop(&s_dewFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_dewFade, 0.25f, 1.0f);
    FloatCurve_AddStop(&s_dewFade, 0.70f, 0.75f);
    FloatCurve_AddStop(&s_dewFade, 1.0f, 0.0f);

    s_mistVeilReady = true;
}

VFX_MistVeilConfig VFX_MistVeil_DefaultConfig(void)
{
    VFX_MistVeilConfig cfg = {0};
    cfg.preset = VFX_MIST_PRESET_RIVER_SHROUD;
    cfg.matId = VC_MAT_WATER;
    cfg.radius = 5.2f;
    cfg.duration = 4.2f;
    cfg.density = 1.0f;
    cfg.buoyancy = 0.04f;
    cfg.swirlSpeed = 0.35f;
    cfg.jetSpeed = 0.0f;
    cfg.snowDensity = 0.25f;
    cfg.volumetricFog = true;
    return cfg;
}

VFX_MistVeilConfig *VFX_MistVeil_GetLiveConfig(void)
{
    if (!s_mistLiveInit) {
        s_mistLiveConfig = VFX_MistVeil_DefaultConfig();
        s_mistLiveInit = true;
    }
    return &s_mistLiveConfig;
}

void VFX_MistVeil_ApplyPreset(VFX_MistVeilConfig *cfg, VFX_MistVeilPreset preset)
{
    if (!cfg) return;
    *cfg = VFX_MistVeil_DefaultConfig();
    cfg->preset = preset;

    switch (preset) {
    case VFX_MIST_PRESET_STEAM_GEYSER:
        cfg->radius = 2.6f;
        cfg->duration = 3.2f;
        cfg->density = 1.35f;
        cfg->buoyancy = 1.95f;
        cfg->swirlSpeed = 1.1f;
        cfg->jetSpeed = 0.0f;
        cfg->snowDensity = 0.35f;
        cfg->volumetricFog = true;
        break;
    case VFX_MIST_PRESET_FROST_BREATH:
        cfg->radius = 2.4f;
        cfg->duration = 2.6f;
        cfg->density = 1.40f;
        cfg->buoyancy = 0.15f;
        cfg->swirlSpeed = 1.6f;
        cfg->jetSpeed = 7.5f;
        cfg->snowDensity = 0.75f;
        cfg->volumetricFog = true;
        break;
    case VFX_MIST_PRESET_CRYO_AURA:
        cfg->radius = 4.5f;
        cfg->duration = 3.8f;
        cfg->density = 1.25f;
        cfg->buoyancy = -0.25f;
        cfg->swirlSpeed = 1.4f;
        cfg->jetSpeed = 0.0f;
        cfg->snowDensity = 0.65f;
        cfg->volumetricFog = true;
        break;
    case VFX_MIST_PRESET_BLIZZARD_VORTEX:
        cfg->radius = 4.8f;
        cfg->duration = 4.0f;
        cfg->density = 1.45f;
        cfg->buoyancy = 0.65f;
        cfg->swirlSpeed = 4.8f;
        cfg->jetSpeed = 0.0f;
        cfg->snowDensity = 1.0f;
        cfg->volumetricFog = true;
        break;
    case VFX_MIST_PRESET_PHASE_STEAM_BURST:
        cfg->radius = 4.2f;
        cfg->duration = 2.4f;
        cfg->density = 1.50f;
        cfg->buoyancy = 1.10f;
        cfg->swirlSpeed = 0.8f;
        cfg->jetSpeed = 4.2f;
        cfg->snowDensity = 0.45f;
        cfg->volumetricFog = true;
        break;
    case VFX_MIST_PRESET_RIVER_SHROUD:
    default:
        break;
    }
    s_mistLastLivePreset = cfg->preset;
}

int VFX_MistVeil_GetParams(VFX_MistVeilConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    if (cfg->preset != s_mistLastLivePreset) {
        VFX_MistVeil_ApplyPreset(cfg, cfg->preset);
    }
    int n = 0;
    #define M_ENUM(lbl, grp, ptr, names, cnt) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_ENUM, (void*)(ptr), 0, (cnt)-1, 0, 0, 0, (names), (cnt)}; \
    } while (0)
    #define M_FLOAT(lbl, grp, ptr, mn, mx, st) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_FLOAT, (void*)(ptr), 0, 0, (mn), (mx), (st), NULL, 0}; \
    } while (0)
    #define M_BOOL(lbl, grp, ptr) do { \
        if (n < maxParams) outParams[n++] = (VFX_ParamDef){lbl, grp, VFX_PARAM_BOOL, (void*)(ptr), 0, 1, 0, 0, 0, NULL, 0}; \
    } while (0)

    M_ENUM("Preset", "Preset", &cfg->preset, s_mistPresetNames, VFX_MIST_PRESET_COUNT);
    M_FLOAT("Radius m", "Volume", &cfg->radius, 1.2f, 9.0f, 0.3f);
    M_FLOAT("Duration s", "Volume", &cfg->duration, 1.0f, 7.0f, 0.25f);
    M_FLOAT("Density", "Optics", &cfg->density, 0.3f, 2.0f, 0.1f);
    M_FLOAT("Buoyancy m/s", "Motion", &cfg->buoyancy, -0.6f, 3.0f, 0.15f);
    M_FLOAT("Swirl rad/s", "Motion", &cfg->swirlSpeed, 0.0f, 7.0f, 0.25f);
    M_FLOAT("Jet Speed m/s", "Motion", &cfg->jetSpeed, 0.0f, 14.0f, 0.5f);
    M_FLOAT("Snow/Ice Motes", "Optics", &cfg->snowDensity, 0.0f, 1.0f, 0.1f);
    M_BOOL("Volumetric Fog", "Optics", &cfg->volumetricFog);

    #undef M_ENUM
    #undef M_FLOAT
    #undef M_BOOL
    return n;
}

void VFX_ComposeMistVeilConfig(Vector3 source, Vector3 target, const VFX_MistVeilConfig *cfgIn)
{
    MistVeil_Init();

    VFX_MistVeilConfig cfg = cfgIn ? *cfgIn : VFX_MistVeil_DefaultConfig();
    float radius = (cfg.radius > 0.2f) ? cfg.radius : 5.0f;
    float duration = (cfg.duration > 0.2f) ? cfg.duration : 4.0f;
    float density = Clamp(cfg.density, 0.2f, 2.5f);
    const VFX_ElementMaterial *mat = VFX_Material(cfg.matId);

    Vector3 dir = Vector3Subtract(target, source);
    dir.y = 0.0f;
    float dLen = Vector3Length(dir);
    Vector3 fwd = (dLen > 0.05f) ? Vector3Scale(dir, 1.0f / dLen) : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = {-fwd.z, 0.0f, fwd.x};

    /* 1. 3D Volumetric Participating Medium (God-rays & atmospheric haze) */
    bool isCryo = (cfg.preset == VFX_MIST_PRESET_FROST_BREATH ||
                   cfg.preset == VFX_MIST_PRESET_CRYO_AURA ||
                   cfg.preset == VFX_MIST_PRESET_BLIZZARD_VORTEX);
    Color volCol = isCryo ? (Color){185, 228, 255, 255} : (Color){215, 238, 250, 255};
    if (mat) {
        volCol = (Color){
            (unsigned char)((volCol.r * 2 + mat->soft.r) / 3),
            (unsigned char)((volCol.g * 2 + mat->soft.g) / 3),
            (unsigned char)((volCol.b * 2 + mat->soft.b) / 3),
            255
        };
    }
    if (cfg.volumetricFog) {
        Vector3 fogCenter = (cfg.preset == VFX_MIST_PRESET_FROST_BREATH)
                                ? Vector3Lerp(source, target, 0.45f)
                                : source;
        FogVolume_SpawnTransient(fogCenter, radius * 1.25f, volCol, 0.22f * density, duration);
    }

    /* 2. Layered Vapor / Steam / Cold Frost Billows */
    int billowCount = (int)(16.0f * (radius / 5.0f) * density);
    if (billowCount < 12) billowCount = 12;
    if (billowCount > 28) billowCount = 28;

    unsigned char baseAlpha = (unsigned char)Clamp(24.0f * density, 14.0f, 52.0f);
    Color baseMist = isCryo
                         ? (Color){192, 232, 255, baseAlpha}
                         : (Color){222, 240, 250, baseAlpha};
    if (mat && !isCryo) {
        baseMist.r = (unsigned char)((baseMist.r * 3 + mat->body.r) / 4);
        baseMist.g = (unsigned char)((baseMist.g * 3 + mat->body.g) / 4);
        baseMist.b = (unsigned char)((baseMist.b * 3 + mat->body.b) / 4);
    }

    for (int i = 0; i < billowCount; i++) {
        float ang = Random01() * 2.0f * PI;
        float tFrac = (float)i / (float)billowCount;
        float rDist = sqrtf(Random01()) * (radius * 0.65f);
        Vector3 p = source;
        Vector3 vel = {0};
        float pRadius = Math_Mix(2.6f, 4.4f, Random01()) * (radius / 5.0f);

        if (cfg.preset == VFX_MIST_PRESET_STEAM_GEYSER) {
            rDist = sqrtf(Random01()) * (radius * 0.40f);
            p = (Vector3){source.x + cosf(ang) * rDist, source.y + 0.15f + Random01() * 0.6f, source.z + sinf(ang) * rDist};
            vel = (Vector3){cosf(ang) * 0.28f, cfg.buoyancy * Math_Mix(0.7f, 1.3f, Random01()), sinf(ang) * 0.28f};
            pRadius = Math_Mix(1.3f, 2.4f, Random01());
        } else if (cfg.preset == VFX_MIST_PRESET_FROST_BREATH) {
            float coneSpread = (Random01() - 0.5f) * 2.0f * (0.35f + 0.45f * tFrac);
            Vector3 jetDir = Vector3Normalize(Vector3Add(fwd, Vector3Scale(right, coneSpread)));
            p = Vector3Add(source, Vector3Add(Vector3Scale(fwd, 0.3f + tFrac * 0.6f),
                                              (Vector3){0.0f, 0.35f + Random01() * 0.4f, 0.0f}));
            vel = Vector3Add(Vector3Scale(jetDir, cfg.jetSpeed * Math_Mix(0.65f, 1.15f, Random01())),
                             (Vector3){0.0f, cfg.buoyancy + (Random01() - 0.5f) * 0.35f, 0.0f});
            pRadius = Math_Mix(1.1f, 2.2f, Random01());
        } else if (cfg.preset == VFX_MIST_PRESET_BLIZZARD_VORTEX) {
            rDist = Math_Mix(0.6f, radius * 0.78f, sqrtf(Random01()));
            float h = Random01() * 2.4f;
            p = (Vector3){source.x + cosf(ang) * rDist, source.y + 0.15f + h, source.z + sinf(ang) * rDist};
            Vector3 tang = {-sinf(ang), 0.0f, cosf(ang)};
            vel = Vector3Add(Vector3Scale(tang, cfg.swirlSpeed * 0.55f),
                             (Vector3){cosf(ang) * 0.25f, cfg.buoyancy * Math_Mix(0.5f, 1.2f, Random01()), sinf(ang) * 0.25f});
            pRadius = Math_Mix(1.8f, 3.2f, Random01());
        } else if (cfg.preset == VFX_MIST_PRESET_PHASE_STEAM_BURST) {
            rDist = Math_Mix(0.3f, radius * 0.35f, Random01());
            p = (Vector3){source.x + cosf(ang) * rDist, source.y + 0.12f + Random01() * 0.35f, source.z + sinf(ang) * rDist};
            vel = (Vector3){cosf(ang) * cfg.jetSpeed * 0.65f, cfg.buoyancy * Math_Mix(0.6f, 1.3f, Random01()), sinf(ang) * cfg.jetSpeed * 0.65f};
            pRadius = Math_Mix(1.8f, 3.0f, Random01());
        } else if (cfg.preset == VFX_MIST_PRESET_CRYO_AURA) {
            /* Ground-hugging katabatic cold vapor ring around base; never blurs upper crystals */
            rDist = Math_Mix(0.45f, 0.95f, sqrtf(Random01())) * radius;
            p = (Vector3){source.x + cosf(ang) * rDist, source.y + 0.04f + Random01() * 0.12f, source.z + sinf(ang) * rDist};
            Vector3 tang = {-sinf(ang), 0.0f, cosf(ang)};
            vel = Vector3Add((Vector3){cosf(ang) * 0.42f, -0.35f, sinf(ang) * 0.42f},
                             Vector3Scale(tang, cfg.swirlSpeed * 0.22f));
            pRadius = Math_Mix(0.38f, 0.68f, Random01());
        } else {
            /* RIVER_SHROUD */
            p = (Vector3){source.x + cosf(ang) * rDist, source.y + 0.06f + Random01() * 0.28f, source.z + sinf(ang) * rDist};
            float driftSpeed = Math_Mix(0.06f, 0.18f, Random01());
            Vector3 tang = {-sinf(ang), 0.0f, cosf(ang)};
            vel = Vector3Add((Vector3){cosf(ang) * driftSpeed, cfg.buoyancy, sinf(ang) * driftSpeed},
                             Vector3Scale(tang, cfg.swirlSpeed * 0.25f));
        }

        float pLife = duration * Math_Mix(0.75f, 1.0f, Random01());
        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = vel,
            .radius = pRadius,
            .lifetime = pLife,
            .colorStart = baseMist,
            .colorEnd = VC_WithAlpha(baseMist, 0),
            .radiusCurve = &s_mistGrow,
            .alphaCurve = &s_mistFade,
            .rotation = Random01() * 2.0f * PI,
            .angularVelocity = (Random01() - 0.5f) * (0.08f + cfg.swirlSpeed * 0.15f),
            .render = {
                .texture = s_mistSoftTex,
                .blendMode = VFX_BLEND_ALPHA,
            }
        });
    }

    /* 3. Condensing Dew Motes / Snowflakes / Ice Crystals */
    int moteCount = (int)((24.0f + 36.0f * cfg.snowDensity) * (radius / 5.0f));
    if (moteCount > 64) moteCount = 64;

    Color sparkCol = isCryo ? (Color){215, 245, 255, 165} : (Color){190, 240, 255, 130};
    if (mat && !isCryo) {
        sparkCol.r = (unsigned char)((sparkCol.r + mat->glow.r) / 2);
        sparkCol.g = (unsigned char)((sparkCol.g + mat->glow.g) / 2);
        sparkCol.b = (unsigned char)((sparkCol.b + mat->glow.b) / 2);
    }

    for (int i = 0; i < moteCount; i++) {
        float ang = Random01() * 2.0f * PI;
        float rDist = sqrtf(Random01()) * radius * 0.85f;
        Vector3 p = {
            source.x + cosf(ang) * rDist,
            source.y + 0.12f + Random01() * (isCryo ? 2.4f : 1.3f),
            source.z + sinf(ang) * rDist
        };
        Vector3 vel = {0};
        if (cfg.preset == VFX_MIST_PRESET_FROST_BREATH) {
            float spread = (Random01() - 0.5f) * 0.8f;
            Vector3 jetDir = Vector3Normalize(Vector3Add(fwd, Vector3Scale(right, spread)));
            p = Vector3Add(source, (Vector3){0.0f, 0.35f + Random01() * 0.5f, 0.0f});
            vel = Vector3Add(Vector3Scale(jetDir, cfg.jetSpeed * Math_Mix(0.8f, 1.35f, Random01())),
                             (Vector3){0.0f, (Random01() - 0.5f) * 0.4f, 0.0f});
        } else if (cfg.preset == VFX_MIST_PRESET_BLIZZARD_VORTEX) {
            Vector3 tang = {-sinf(ang), 0.0f, cosf(ang)};
            vel = Vector3Add(Vector3Scale(tang, cfg.swirlSpeed * 0.85f),
                             (Vector3){0.0f, -0.35f + Random01() * 0.65f, 0.0f});
        } else {
            vel = (Vector3){
                (Random01() - 0.5f) * 0.18f,
                (isCryo ? -0.18f : 0.12f) + cfg.buoyancy * 0.35f + Random01() * 0.15f,
                (Random01() - 0.5f) * 0.18f
            };
        }

        float mLife = Math_Mix(1.2f, 2.5f, Random01());
        SpawnParticle((ParticleConfig){
            .position = p,
            .velocity = vel,
            .radius = Math_Mix(0.025f, 0.062f, Random01()),
            .lifetime = mLife,
            .colorStart = sparkCol,
            .colorEnd = VC_WithAlpha(sparkCol, 0),
            .alphaCurve = &s_dewFade,
            .render = {
                .texture = s_dewTex,
                .blendMode = VFX_BLEND_ADDITIVE,
                .unlit = 1,
                .emissiveBoost = isCryo ? 1.85f : 1.50f,
            }
        });
    }
}

void VFX_ComposeMistVeilEx(Vector3 pos, VC_MaterialId matId, float radius, float duration)
{
    VFX_MistVeilConfig cfg = VFX_MistVeil_DefaultConfig();
    cfg.matId = matId;
    if (radius > 0.0f) cfg.radius = radius;
    if (duration > 0.0f) cfg.duration = duration;
    VFX_ComposeMistVeilConfig(pos, Vector3Add(pos, (Vector3){4.5f, 0.0f, 0.0f}), &cfg);
}

void VFX_ComposeMistVeil(Vector3 pos, float radius, float duration)
{
    VFX_ComposeMistVeilEx(pos, VC_MAT_WATER, radius, duration);
}
