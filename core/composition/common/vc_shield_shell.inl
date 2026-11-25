// ── Unified Wuxia Shield (merged ShieldShell + FlowShield) ──────────────────
// 9 procedural martial/xianxia/glass presets + 4 ground-cut modes + impact ripples.
// Far wall first (RL_CULL_FACE_FRONT) then near wall (RL_CULL_FACE_BACK),
// dual-pass BODY (VFX_SURFACE_ALPHA) + EMISSION (VFX_SURFACE_PREMULTIPLIED).

#include "core/tuning.h"
#include "core/gfx_quality.h"

#define VFX_SHIELD_SHELL_MAX 8
#define SHIELD_SHELL_TRI_BUDGET 6400
#define SHIELD_SHELL_RINGS_LOW 14
#define SHIELD_SHELL_RINGS_MED 24
#define SHIELD_SHELL_RINGS_HIGH 40
#define SHIELD_MAX_IMPACTS 4

#ifndef SHIELD_BURIED_LIFT
#define SHIELD_BURIED_LIFT 0.5f
#endif

static int ShieldShell_Rings(int activeCount)
{
    GfxQuality q = GfxQuality_Get();
    int ceiling = (q >= GFX_HIGH) ? SHIELD_SHELL_RINGS_HIGH
                : (q >= GFX_MED)  ? SHIELD_SHELL_RINGS_MED
                                  : SHIELD_SHELL_RINGS_LOW;
    if (q <= GFX_LOW) return SHIELD_SHELL_RINGS_LOW;
    if (activeCount < 1) activeCount = 1;
    int rings = SHIELD_SHELL_RINGS_LOW;
    while (rings + 2 <= ceiling &&
           activeCount * (rings + 2) * (rings + 2) * 2 <= SHIELD_SHELL_TRI_BUDGET)
        rings += 2;
    return rings;
}

typedef struct {
    const char *name;
    Vector3 primary;
    Vector3 rim;
    float opacity;
    float fresnel;
    float seam;
    float speed;
    float backWall;
    float relief;
    float emission;
    float refraction;
} WuxiaShieldPresetDef;

static const WuxiaShieldPresetDef k_shieldPresets[VFX_SHIELD_PRESET_COUNT] = {
    // 0: Hộ Thể Chân Khí
    { "CHAN KHI",   { 0.620f, 0.851f, 0.796f }, { 0.941f, 0.984f, 0.969f }, 0.88f, 1.15f, 1.05f, 0.95f, 0.40f, 1.00f, 1.05f, 0.010f },
    // 1: Thủy Mặc Bát Quái
    { "BAGUA",      { 0.847f, 0.812f, 0.753f }, { 0.961f, 0.929f, 0.878f }, 0.94f, 1.12f, 1.10f, 0.85f, 0.42f, 1.00f, 1.15f, 0.008f },
    // 2: Kim Chung Tráo
    { "KIM CHUNG",  { 0.949f, 0.639f, 0.161f }, { 1.000f, 0.941f, 0.761f }, 0.92f, 1.22f, 1.10f, 0.85f, 0.38f, 1.00f, 1.35f, 0.012f },
    // 3: Ngọc Thanh Tiên Tráo
    { "NGOC THANH", { 0.149f, 0.702f, 0.553f }, { 0.698f, 0.980f, 0.886f }, 0.90f, 1.20f, 1.08f, 0.80f, 0.42f, 1.00f, 1.25f, 0.014f },
    // 4: Lôi Đình Độ Kiếp
    { "LOI DINH",   { 0.427f, 0.365f, 0.988f }, { 0.549f, 0.925f, 1.000f }, 0.94f, 1.28f, 1.15f, 1.15f, 0.40f, 1.00f, 1.65f, 0.012f },
    // 5: Xích Viêm Hỏa Tráo
    { "XICH VIEM",  { 0.878f, 0.282f, 0.098f }, { 1.000f, 0.820f, 0.478f }, 0.92f, 1.24f, 1.18f, 1.05f, 0.40f, 1.00f, 1.55f, 0.012f },
    // 6: Huyền Băng Kính
    { "HUYEN BANG", { 0.431f, 0.796f, 0.961f }, { 0.929f, 0.980f, 1.000f }, 0.90f, 1.20f, 1.10f, 0.75f, 0.42f, 1.00f, 1.20f, 0.018f },
    // 7: Lưu Ly Bảo Châu
    { "LUU LY",     { 0.784f, 0.925f, 1.000f }, { 1.000f, 1.000f, 1.000f }, 0.90f, 1.18f, 1.10f, 0.85f, 0.45f, 1.00f, 1.00f, 0.024f },
    // 8: Thanh Phong Hộ Tráo
    { "PHONG TRAO", { 0.341f, 0.890f, 0.761f }, { 0.918f, 1.000f, 0.980f }, 0.90f, 1.22f, 1.12f, 1.15f, 0.38f, 1.05f, 1.25f, 0.016f },
};

static const char *k_shieldPresetNames[VFX_SHIELD_PRESET_COUNT] = {
    "CHAN KHI", "BAGUA", "KIM CHUNG", "NGOC THANH", "LOI DINH",
    "XICH VIEM", "HUYEN BANG", "LUU LY", "PHONG TRAO"
};

static const char *k_shieldGroundModeNames[VFX_SHIELD_GROUND_COUNT] = {
    "Hemi (50%)", "Sunken (75%)", "Dome (30%)", "Full (100%)"
};

typedef struct {
    bool active, stopping;
    Vector3 pos;
    VC_MaterialId mat;
    float radius, target, level, elapsed;
    VFX_ShieldDesc desc;
    Vector3 impactDir[SHIELD_MAX_IMPACTS];
    float   impactTime[SHIELD_MAX_IMPACTS];
    int     nextImpactSlot;
} VC_ShieldShell;

typedef struct {
    Shader shader;
    int camPos, camRight, camUp, camForward;
    int shellCenter, shellRadius;
    int shieldTime, speed, preset;
    int groundEnabled, groundY;
    int hasImpacts, impacts;
    int primaryColor, rimColor;
    int opacity, fresnel, seamIntensity, backWall, passSide, relief, refraction, emissionGain, premultiply;
    int noiseTex, sceneTex, hasScene, depthTex, hasDepth, depthClip;
} VC_ShieldShader;

typedef struct {
    Shader shader;
    int preset, primaryColor, rimColor;
    int opacity, seamIntensity, speed, shieldTime, emissionGain, premultiply;
    int rcut, quadHalf, noiseTex;
} VC_ShieldGroundShader;

static VC_ShieldShell s_shieldShells[VFX_SHIELD_SHELL_MAX];
static VC_ShieldShader s_shieldShader = {0};
static VC_ShieldGroundShader s_shieldGroundShader = {0};
static Texture2D s_shieldNoiseTex = {0};
static bool s_shieldInit = false;
static float s_shieldDepthEnabled = 1.0f;

static float ShieldShell_Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float Shield_ElevationForMode(VFX_ShieldGroundMode mode)
{
    switch (mode) {
    case VFX_SHIELD_GROUND_HEMI:   return  0.00f;
    case VFX_SHIELD_GROUND_SUNKEN: return  0.50f;
    case VFX_SHIELD_GROUND_DOME30: return -0.40f;
    case VFX_SHIELD_GROUND_FULL:
    default:                       return  1.02f;
    }
}

static Vector3 Shield_CenterForDesc(Vector3 groundPos, float radius, const VFX_ShieldDesc *d)
{
    if (!d) return groundPos;
    // Airborne FULL shields with elevation==0 (e.g. RiftBolt) stay centered at pos.
    if (d->groundMode == VFX_SHIELD_GROUND_FULL && fabsf(d->elevation) < 0.001f)
        return groundPos;
    return (Vector3){ groundPos.x, groundPos.y + radius * d->elevation, groundPos.z };
}

static bool Shield_IntersectsGround(const VFX_ShieldDesc *d)
{
    if (!d) return false;
    if (d->groundMode == VFX_SHIELD_GROUND_FULL && fabsf(d->elevation) < 0.001f)
        return false;
    return (d->elevation > -0.995f && d->elevation < 0.995f);
}

static float Shield_TileHash(int ix, int iy, int period, float seed)
{
    int x = ((ix % period) + period) % period;
    int y = ((iy % period) + period) % period;
    double n = sin((double)x * 127.1 + (double)y * 311.7 + (double)seed * 74.7) * 43758.5453123;
    return (float)(n - floor(n));
}

static float Shield_TileNoise(float u, float v, int period, float seed)
{
    float x = u * (float)period;
    float y = v * (float)period;
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = Shield_TileHash(ix,     iy,     period, seed);
    float b = Shield_TileHash(ix + 1, iy,     period, seed);
    float c = Shield_TileHash(ix,     iy + 1, period, seed);
    float d = Shield_TileHash(ix + 1, iy + 1, period, seed);
    float ab = a + (b - a) * ux;
    float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
}

static float Shield_TileFbm(float u, float v, int basePeriod, float seed)
{
    float sum = 0.0f, amp = 0.56f;
    int p = basePeriod;
    for (int o = 0; o < 3; ++o) {
        sum += amp * Shield_TileNoise(u, v, p, seed + (float)o * 13.7f);
        p *= 2;
        amp *= 0.5f;
    }
    return ShieldShell_Clamp01(sum / 0.98f);
}

static float Shield_TileRidged(float u, float v, int basePeriod, float seed)
{
    float sum = 0.0f, amp = 0.62f;
    int p = basePeriod;
    for (int o = 0; o < 3; ++o) {
        float n = Shield_TileNoise(u, v, p, seed + (float)o * 19.3f);
        float ridge = 1.0f - fabsf(n * 2.0f - 1.0f);
        sum += amp * ridge * ridge;
        p *= 2;
        amp *= 0.48f;
    }
    return ShieldShell_Clamp01(sum / 1.06f);
}

static Texture2D Shield_CreateProceduralNoiseTexture(void)
{
    enum { NSIZE = 256 };
    static unsigned char pixels[NSIZE * NSIZE * 4];
    int ptr = 0;
    for (int y = 0; y < NSIZE; ++y) {
        float v = (float)y / (float)NSIZE;
        for (int x = 0; x < NSIZE; ++x) {
            float u = (float)x / (float)NSIZE;
            pixels[ptr++] = (unsigned char)(Shield_TileFbm(u, v, 4, 1.7f) * 255.0f + 0.5f);
            pixels[ptr++] = (unsigned char)(Shield_TileFbm(u, v, 5, 11.3f) * 255.0f + 0.5f);
            pixels[ptr++] = (unsigned char)(Shield_TileRidged(u, v, 6, 29.1f) * 255.0f + 0.5f);
            pixels[ptr++] = (unsigned char)(Shield_TileRidged(u, v, 12, 47.9f) * 255.0f + 0.5f);
        }
    }
    Image img = {
        .data = pixels,
        .width = NSIZE,
        .height = NSIZE,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D tex = LoadTextureFromImage(img);
    if (tex.id != 0) {
        SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    }
    return tex;
}

static void ShieldShell_InitShared(void)
{
    if (s_shieldInit) return;
    s_shieldShader.shader = ResourceManager_LoadShader("core/shaders/glass_shell.vs", "core/shaders/wuxia_shield.fs");
    s_shieldNoiseTex = Shield_CreateProceduralNoiseTexture();

    Shader sh = s_shieldShader.shader;
    if (sh.id != 0) {
        s_shieldShader.camPos        = GetShaderLocation(sh, "u_camPos");
        s_shieldShader.camRight      = GetShaderLocation(sh, "u_camRight");
        s_shieldShader.camUp         = GetShaderLocation(sh, "u_camUp");
        s_shieldShader.camForward    = GetShaderLocation(sh, "u_camForward");
        s_shieldShader.shellCenter   = GetShaderLocation(sh, "u_shellCenter");
        s_shieldShader.shellRadius   = GetShaderLocation(sh, "u_shellRadius");
        s_shieldShader.shieldTime    = GetShaderLocation(sh, "u_shieldTime");
        s_shieldShader.speed         = GetShaderLocation(sh, "u_speed");
        s_shieldShader.preset        = GetShaderLocation(sh, "u_preset");
        s_shieldShader.groundEnabled = GetShaderLocation(sh, "u_groundEnabled");
        s_shieldShader.groundY       = GetShaderLocation(sh, "u_groundY");
        s_shieldShader.hasImpacts    = GetShaderLocation(sh, "u_hasImpacts");
        s_shieldShader.impacts       = GetShaderLocation(sh, "u_impacts");
        s_shieldShader.primaryColor  = GetShaderLocation(sh, "u_primaryColor");
        s_shieldShader.rimColor      = GetShaderLocation(sh, "u_rimColor");
        s_shieldShader.opacity       = GetShaderLocation(sh, "u_opacity");
        s_shieldShader.fresnel       = GetShaderLocation(sh, "u_fresnel");
        s_shieldShader.seamIntensity = GetShaderLocation(sh, "u_seamIntensity");
        s_shieldShader.backWall      = GetShaderLocation(sh, "u_backWall");
        s_shieldShader.passSide      = GetShaderLocation(sh, "u_passSide");
        s_shieldShader.relief        = GetShaderLocation(sh, "u_relief");
        s_shieldShader.refraction    = GetShaderLocation(sh, "u_refraction");
        s_shieldShader.emissionGain  = GetShaderLocation(sh, "u_emissionGain");
        s_shieldShader.premultiply   = GetShaderLocation(sh, "u_premultiply");
        s_shieldShader.noiseTex      = GetShaderLocation(sh, "u_noiseTex");
        s_shieldShader.sceneTex      = GetShaderLocation(sh, "u_sceneTex");
        s_shieldShader.hasScene      = GetShaderLocation(sh, "u_hasScene");
        s_shieldShader.depthTex      = GetShaderLocation(sh, "u_depthTex");
        s_shieldShader.hasDepth      = GetShaderLocation(sh, "u_hasDepth");
        s_shieldShader.depthClip     = GetShaderLocation(sh, "u_depthClip");
    }

    s_shieldGroundShader.shader = ResourceManager_LoadShader("core/shaders/wuxia_shield_ground.vs", "core/shaders/wuxia_shield_ground.fs");
    Shader gsh = s_shieldGroundShader.shader;
    if (gsh.id != 0) {
        s_shieldGroundShader.preset        = GetShaderLocation(gsh, "u_preset");
        s_shieldGroundShader.primaryColor  = GetShaderLocation(gsh, "u_primaryColor");
        s_shieldGroundShader.rimColor      = GetShaderLocation(gsh, "u_rimColor");
        s_shieldGroundShader.opacity       = GetShaderLocation(gsh, "u_opacity");
        s_shieldGroundShader.seamIntensity = GetShaderLocation(gsh, "u_seamIntensity");
        s_shieldGroundShader.speed         = GetShaderLocation(gsh, "u_speed");
        s_shieldGroundShader.shieldTime    = GetShaderLocation(gsh, "u_shieldTime");
        s_shieldGroundShader.emissionGain  = GetShaderLocation(gsh, "u_emissionGain");
        s_shieldGroundShader.premultiply   = GetShaderLocation(gsh, "u_premultiply");
        s_shieldGroundShader.rcut          = GetShaderLocation(gsh, "u_rcut");
        s_shieldGroundShader.quadHalf      = GetShaderLocation(gsh, "u_quadHalf");
        s_shieldGroundShader.noiseTex      = GetShaderLocation(gsh, "u_noiseTex");
    }
    Tuning_RegisterFloat("shield_shell_depth_enabled", &s_shieldDepthEnabled, 1.0f);
    s_shieldInit = true;
}

VFX_ShieldDesc VFX_Shield_DefaultDesc(VFX_ShieldPreset preset)
{
    if (preset < 0 || preset >= VFX_SHIELD_PRESET_COUNT) preset = VFX_SHIELD_PRESET_CHANKHI;
    const WuxiaShieldPresetDef *p = &k_shieldPresets[preset];
    VFX_ShieldDesc d = {0};
    d.preset          = preset;
    d.groundMode      = VFX_SHIELD_GROUND_HEMI;
    d.elevation       = Shield_ElevationForMode(VFX_SHIELD_GROUND_HEMI);
    d.opacity         = p->opacity;
    d.fresnel         = p->fresnel;
    d.seam            = p->seam;
    d.speed           = p->speed;
    d.backWall        = p->backWall;
    d.relief          = p->relief;
    d.emission        = p->emission;
    d.refraction      = p->refraction;
    d.usePresetColors = true;
    return d;
}

const char *VFX_ShieldPreset_Name(VFX_ShieldPreset preset)
{
    if (preset < 0 || preset >= VFX_SHIELD_PRESET_COUNT) return "CHAN KHI";
    return k_shieldPresetNames[preset];
}

const char *VFX_ShieldGroundMode_Name(VFX_ShieldGroundMode mode)
{
    if (mode < 0 || mode >= VFX_SHIELD_GROUND_COUNT) return "Hemi (50%)";
    return k_shieldGroundModeNames[mode];
}

int VFX_Shield_GetParams(VFX_ShieldDesc *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Preset", .group = "Wuxia Shield", .type = VFX_PARAM_ENUM,
        .valPtr = (void *)&cfg->preset,
        .enumNames = k_shieldPresetNames, .enumCount = VFX_SHIELD_PRESET_COUNT,
        .minInt = 0, .maxInt = VFX_SHIELD_PRESET_COUNT - 1, .stepFloat = 1.0f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Ground Cut", .group = "Wuxia Shield", .type = VFX_PARAM_ENUM,
        .valPtr = (void *)&cfg->groundMode,
        .enumNames = k_shieldGroundModeNames, .enumCount = VFX_SHIELD_GROUND_COUNT,
        .minInt = 0, .maxInt = VFX_SHIELD_GROUND_COUNT - 1, .stepFloat = 1.0f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Elevation", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->elevation, .minFloat = -0.85f, .maxFloat = 1.25f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Opacity", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->opacity, .minFloat = 0.1f, .maxFloat = 1.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Fresnel", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->fresnel, .minFloat = 0.2f, .maxFloat = 2.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Seam Glow", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->seam, .minFloat = 0.0f, .maxFloat = 2.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Speed", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->speed, .minFloat = 0.1f, .maxFloat = 2.5f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Back Wall", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->backWall, .minFloat = 0.0f, .maxFloat = 1.0f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Relief", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->relief, .minFloat = 0.25f, .maxFloat = 2.0f, .stepFloat = 0.05f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Emission", .group = "Wuxia Shield", .type = VFX_PARAM_FLOAT,
        .valPtr = (void *)&cfg->emission, .minFloat = 0.5f, .maxFloat = 8.0f, .stepFloat = 0.2f
    };
    if (n < max) out[n++] = (VFX_ParamDef){
        .name = "Preset Colors", .group = "Wuxia Shield", .type = VFX_PARAM_BOOL,
        .valPtr = (void *)&cfg->usePresetColors
    };

    return n;
}

int VFX_Shield_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius, float intensity, const VFX_ShieldDesc *desc)
{
    ShieldShell_InitShared();
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        if (s_shieldShells[i].active) continue;
        VC_ShieldShell *sh = &s_shieldShells[i];
        *sh = (VC_ShieldShell){0};
        sh->active  = true;
        sh->pos     = pos;
        sh->mat     = mat;
        sh->radius  = (radius > 0.0f) ? radius : 1.0f;
        sh->target  = ShieldShell_Clamp01(intensity);
        sh->desc    = desc ? *desc : VFX_Shield_DefaultDesc(VFX_SHIELD_PRESET_CHANKHI);
        for (int k = 0; k < SHIELD_MAX_IMPACTS; ++k) sh->impactTime[k] = -10.0f;
        return i;
    }
    return -1;
}

void VFX_Shield_SetGroundMode(int handle, VFX_ShieldGroundMode mode)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) {
        s_shieldShells[handle].desc.groundMode = mode;
        s_shieldShells[handle].desc.elevation  = (mode == VFX_SHIELD_GROUND_FULL) ? 0.0f : Shield_ElevationForMode(mode);
    }
}

void VFX_Shield_SetDesc(int handle, const VFX_ShieldDesc *desc)
{
    if (desc && handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
        s_shieldShells[handle].desc = *desc;
}

void VFX_Shield_TriggerImpact(int handle, Vector3 impactWorld)
{
    if (handle < 0 || handle >= VFX_SHIELD_SHELL_MAX || !s_shieldShells[handle].active) return;
    VC_ShieldShell *sh = &s_shieldShells[handle];
    Vector3 center = Shield_CenterForDesc(sh->pos, sh->radius, &sh->desc);
    Vector3 d = Vector3Subtract(impactWorld, center);
    if (Vector3LengthSqr(d) < 1e-5f) d = (Vector3){ 0.0f, 0.5f, 0.866f };
    sh->impactDir[sh->nextImpactSlot]  = Vector3Normalize(d);
    sh->impactTime[sh->nextImpactSlot] = sh->elapsed;
    sh->nextImpactSlot = (sh->nextImpactSlot + 1) % SHIELD_MAX_IMPACTS;
}

// ── Compatibility wrappers (ShieldShell & FlowShield) ────────────────────────

int VFX_ShieldShell_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius,
                            float intensity, const VFX_ShieldSurface *surface)
{
    (void)surface;
    VFX_ShieldDesc d = VFX_Shield_DefaultDesc(VFX_SHIELD_PRESET_CHANKHI);
    d.groundMode = VFX_SHIELD_GROUND_SUNKEN;
    return VFX_Shield_SpawnEx(pos, mat, radius, intensity, &d);
}

int VFX_ShieldShell_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_SpawnEx(pos, mat, radius, intensity, NULL); }

int VFX_ComposeShieldShell(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_ShieldShell_Spawn(pos, mat, radius, intensity); }

void VFX_ShieldShell_SetTransform(int handle, Vector3 pos)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
        s_shieldShells[handle].pos = pos;
}

void VFX_ShieldShell_SetIntensity(int handle, float intensity01)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) {
        s_shieldShells[handle].target = ShieldShell_Clamp01(intensity01);
        s_shieldShells[handle].stopping = false;
    }
}

void VFX_ShieldShell_SetSurface(int handle, const VFX_ShieldSurface *surface)
{ (void)handle; (void)surface; }

void VFX_ShieldShell_SetImpact(int handle, Vector3 impactWorld, float timeSinceImpact)
{
    (void)timeSinceImpact;
    VFX_Shield_TriggerImpact(handle, impactWorld);
}

void VFX_ShieldShell_SetGroundContact(int handle, bool onGround)
{
    VFX_Shield_SetGroundMode(handle, onGround ? VFX_SHIELD_GROUND_SUNKEN : VFX_SHIELD_GROUND_FULL);
}

void VFX_ShieldShell_Stop(int handle)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) {
        s_shieldShells[handle].stopping = true;
        s_shieldShells[handle].target = 0.0f;
    }
}

void VFX_KillShieldShell(int handle)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX) s_shieldShells[handle].active = false; }

// FlowShield compatibility routes to unified shield (NGOCTHANH / CHANKHI)
int VFX_FlowShield_Spawn(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{
    VFX_ShieldDesc d = VFX_Shield_DefaultDesc(VFX_SHIELD_PRESET_NGOCTHANH);
    d.groundMode = VFX_SHIELD_GROUND_SUNKEN;
    d.usePresetColors = (mat == VC_MAT_WATER);
    return VFX_Shield_SpawnEx(pos, mat, radius, intensity, &d);
}

int VFX_ComposeFlowShield(Vector3 pos, VC_MaterialId mat, float radius, float intensity)
{ return VFX_FlowShield_Spawn(pos, mat, radius, intensity); }

void VFX_FlowShield_SetTransform(int handle, Vector3 pos) { VFX_ShieldShell_SetTransform(handle, pos); }
void VFX_FlowShield_SetIntensity(int handle, float intensity01) { VFX_ShieldShell_SetIntensity(handle, intensity01); }
void VFX_FlowShield_Stop(int handle) { VFX_ShieldShell_Stop(handle); }
void VFX_KillFlowShield(int handle) { VFX_KillShieldShell(handle); }
void VFX_FlowShield_DrawRefraction(Camera3D cam) { (void)cam; }

// ── Update & Draw ────────────────────────────────────────────────────────────

static void VC_ShieldShell_Update(float dt)
{
    bool anyActive = false;
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        VC_ShieldShell *sh = &s_shieldShells[i];
        if (!sh->active) continue;
        anyActive = true;
        if (!s_shieldInit) ShieldShell_InitShared();
        sh->elapsed += dt;
        sh->level += (sh->target - sh->level) * (1.0f - expf(-dt * 7.0f));
        if (sh->stopping && sh->level < 0.004f) {
            sh->active = false;
        }
    }
    if (anyActive) SceneTargets_RequestSceneSnapshot();
}

static void ShieldShell_BindSharedInputs(Camera3D cam, Vector3 right, Vector3 up, Vector3 fwd)
{
    Shader sh = s_shieldShader.shader;
    if (s_shieldShader.camPos >= 0)     SetShaderValue(sh, s_shieldShader.camPos,     &cam.position, SHADER_UNIFORM_VEC3);
    if (s_shieldShader.camRight >= 0)   SetShaderValue(sh, s_shieldShader.camRight,   &right,        SHADER_UNIFORM_VEC3);
    if (s_shieldShader.camUp >= 0)      SetShaderValue(sh, s_shieldShader.camUp,      &up,           SHADER_UNIFORM_VEC3);
    if (s_shieldShader.camForward >= 0) SetShaderValue(sh, s_shieldShader.camForward, &fwd,          SHADER_UNIFORM_VEC3);

    if (s_shieldShader.noiseTex >= 0 && s_shieldNoiseTex.id != 0)
        SetShaderValueTexture(sh, s_shieldShader.noiseTex, s_shieldNoiseTex);

    Texture2D sceneTex = SceneTargets_GetSceneSnapshotTexture();
    int hasScene = (sceneTex.id != 0) ? 1 : 0;
    if (s_shieldShader.hasScene >= 0) SetShaderValue(sh, s_shieldShader.hasScene, &hasScene, SHADER_UNIFORM_INT);
    if (hasScene && s_shieldShader.sceneTex >= 0) SetShaderValueTexture(sh, s_shieldShader.sceneTex, sceneTex);

    Texture2D depthTex = SceneTargets_GetDepthTexture();
    int hasDepth = (depthTex.id != 0 && s_shieldDepthEnabled > 0.5f) ? 1 : 0;
    if (s_shieldShader.hasDepth >= 0) SetShaderValue(sh, s_shieldShader.hasDepth, &hasDepth, SHADER_UNIFORM_INT);
    if (hasDepth && s_shieldShader.depthTex >= 0) SetShaderValueTexture(sh, s_shieldShader.depthTex, depthTex);

    Vector2 clip = { 0.1f, 200.0f };
    if (s_shieldShader.depthClip >= 0) SetShaderValue(sh, s_shieldShader.depthClip, &clip, SHADER_UNIFORM_VEC2);
}

static void ShieldShell_DrawPass(bool emissionPass, float passSide)
{
    int activeCount = 0;
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
        if (s_shieldShells[i].active) activeCount++;
    int rings = ShieldShell_Rings(activeCount);
    Shader sh = s_shieldShader.shader;
    float premul = emissionPass ? 1.0f : 0.0f;

    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        const VC_ShieldShell *shObj = &s_shieldShells[i];
        if (!shObj->active || shObj->level <= 0.004f) continue;

        float radius = shObj->radius * (0.38f + 0.62f * shObj->level);
        if (radius <= 0.01f) continue;

        const VFX_ShieldDesc *d = &shObj->desc;
        int presetIdx = (d->preset >= 0 && d->preset < VFX_SHIELD_PRESET_COUNT) ? (int)d->preset : 0;
        const WuxiaShieldPresetDef *pdef = &k_shieldPresets[presetIdx];

        Vector3 center = Shield_CenterForDesc(shObj->pos, radius, d);
        float groundEnabled = Shield_IntersectsGround(d) ? 1.0f : 0.0f;
        float groundY = shObj->pos.y;

        Vector3 primaryCol = pdef->primary;
        Vector3 rimCol     = pdef->rim;
        if (!d->usePresetColors) {
            const VFX_ElementMaterial *m = VFX_Material(shObj->mat);
            Vector4 bc = ColorNormalize(m->body);
            Vector4 gc = ColorNormalize(m->glow);
            primaryCol = (Vector3){ bc.x, bc.y, bc.z };
            rimCol     = (Vector3){ gc.x, gc.y, gc.z };
        }

        Vector4 impVecs[SHIELD_MAX_IMPACTS];
        float hasImp = 0.0f;
        for (int k = 0; k < SHIELD_MAX_IMPACTS; ++k) {
            float age = shObj->elapsed - shObj->impactTime[k];
            if (shObj->impactTime[k] >= 0.0f && age >= 0.0f && age < 2.6f) {
                impVecs[k] = (Vector4){ shObj->impactDir[k].x, shObj->impactDir[k].y, shObj->impactDir[k].z, age };
                hasImp = 1.0f;
            } else {
                impVecs[k] = (Vector4){ 0.0f, 1.0f, 0.0f, -1.0f };
            }
        }

        float opacity = d->opacity * shObj->level;
        if (s_shieldShader.shellCenter >= 0)   SetShaderValue(sh, s_shieldShader.shellCenter,   &center,        SHADER_UNIFORM_VEC3);
        if (s_shieldShader.shellRadius >= 0)   SetShaderValue(sh, s_shieldShader.shellRadius,   &radius,        SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.shieldTime >= 0)    SetShaderValue(sh, s_shieldShader.shieldTime,    &shObj->elapsed, SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.speed >= 0)         SetShaderValue(sh, s_shieldShader.speed,         &d->speed,      SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.preset >= 0)        SetShaderValue(sh, s_shieldShader.preset,        &presetIdx,     SHADER_UNIFORM_INT);
        if (s_shieldShader.groundEnabled >= 0) SetShaderValue(sh, s_shieldShader.groundEnabled, &groundEnabled, SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.groundY >= 0)       SetShaderValue(sh, s_shieldShader.groundY,       &groundY,       SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.hasImpacts >= 0)    SetShaderValue(sh, s_shieldShader.hasImpacts,    &hasImp,        SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.impacts >= 0)       SetShaderValueV(sh, s_shieldShader.impacts,      impVecs,        SHADER_UNIFORM_VEC4, SHIELD_MAX_IMPACTS);
        if (s_shieldShader.primaryColor >= 0)  SetShaderValue(sh, s_shieldShader.primaryColor,  &primaryCol,    SHADER_UNIFORM_VEC3);
        if (s_shieldShader.rimColor >= 0)      SetShaderValue(sh, s_shieldShader.rimColor,      &rimCol,        SHADER_UNIFORM_VEC3);
        if (s_shieldShader.opacity >= 0)       SetShaderValue(sh, s_shieldShader.opacity,       &opacity,       SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.fresnel >= 0)       SetShaderValue(sh, s_shieldShader.fresnel,       &d->fresnel,    SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.seamIntensity >= 0) SetShaderValue(sh, s_shieldShader.seamIntensity, &d->seam,       SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.backWall >= 0)      SetShaderValue(sh, s_shieldShader.backWall,      &d->backWall,   SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.passSide >= 0)      SetShaderValue(sh, s_shieldShader.passSide,      &passSide,      SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.relief >= 0)        SetShaderValue(sh, s_shieldShader.relief,        &d->relief,     SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.refraction >= 0)    SetShaderValue(sh, s_shieldShader.refraction,    &d->refraction, SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.emissionGain >= 0)  SetShaderValue(sh, s_shieldShader.emissionGain,  &d->emission,   SHADER_UNIFORM_FLOAT);
        if (s_shieldShader.premultiply >= 0)   SetShaderValue(sh, s_shieldShader.premultiply,   &premul,        SHADER_UNIFORM_FLOAT);

        rlSetTexture(s_shieldNoiseTex.id);
        DrawCoreSphere(center, radius, rings, rings, WHITE);
        rlDrawRenderBatchActive();
        rlSetTexture(0);
    }
}

static void ShieldShell_DrawGroundPass(bool emissionPass)
{
    Shader gsh = s_shieldGroundShader.shader;
    if (gsh.id == 0) return;

    float premul = emissionPass ? 1.0f : 0.0f;
    bool started = false;

    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        const VC_ShieldShell *shObj = &s_shieldShells[i];
        if (!shObj->active || shObj->level <= 0.004f) continue;
        const VFX_ShieldDesc *d = &shObj->desc;
        if (!Shield_IntersectsGround(d)) continue;

        float radius = shObj->radius * (0.38f + 0.62f * shObj->level);
        if (radius <= 0.01f) continue;

        Vector3 center = Shield_CenterForDesc(shObj->pos, radius, d);
        float dy = shObj->pos.y - center.y;
        float rcutSq = radius * radius - dy * dy;
        if (rcutSq <= 0.002f) continue;
        float rcut = sqrtf(rcutSq);
        float quadHalf = rcut * 2.40f;

        if (!started) {
            SkillManager_BeginShader(gsh);
            if (s_shieldGroundShader.noiseTex >= 0 && s_shieldNoiseTex.id != 0)
                SetShaderValueTexture(gsh, s_shieldGroundShader.noiseTex, s_shieldNoiseTex);
            rlDisableBackfaceCulling();
            started = true;
        }

        int presetIdx = (d->preset >= 0 && d->preset < VFX_SHIELD_PRESET_COUNT) ? (int)d->preset : 0;
        const WuxiaShieldPresetDef *pdef = &k_shieldPresets[presetIdx];
        Vector3 primaryCol = pdef->primary;
        Vector3 rimCol     = pdef->rim;
        if (!d->usePresetColors) {
            const VFX_ElementMaterial *m = VFX_Material(shObj->mat);
            Vector4 bc = ColorNormalize(m->body);
            Vector4 gc = ColorNormalize(m->glow);
            primaryCol = (Vector3){ bc.x, bc.y, bc.z };
            rimCol     = (Vector3){ gc.x, gc.y, gc.z };
        }

        float opacity = d->opacity * shObj->level;
        if (s_shieldGroundShader.preset >= 0)        SetShaderValue(gsh, s_shieldGroundShader.preset,        &presetIdx,     SHADER_UNIFORM_INT);
        if (s_shieldGroundShader.primaryColor >= 0)  SetShaderValue(gsh, s_shieldGroundShader.primaryColor,  &primaryCol,    SHADER_UNIFORM_VEC3);
        if (s_shieldGroundShader.rimColor >= 0)      SetShaderValue(gsh, s_shieldGroundShader.rimColor,      &rimCol,        SHADER_UNIFORM_VEC3);
        if (s_shieldGroundShader.opacity >= 0)       SetShaderValue(gsh, s_shieldGroundShader.opacity,       &opacity,       SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.seamIntensity >= 0) SetShaderValue(gsh, s_shieldGroundShader.seamIntensity, &d->seam,       SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.speed >= 0)         SetShaderValue(gsh, s_shieldGroundShader.speed,         &d->speed,      SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.shieldTime >= 0)    SetShaderValue(gsh, s_shieldGroundShader.shieldTime,    &shObj->elapsed, SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.emissionGain >= 0)  SetShaderValue(gsh, s_shieldGroundShader.emissionGain,  &d->emission,   SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.premultiply >= 0)   SetShaderValue(gsh, s_shieldGroundShader.premultiply,   &premul,        SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.rcut >= 0)          SetShaderValue(gsh, s_shieldGroundShader.rcut,          &rcut,          SHADER_UNIFORM_FLOAT);
        if (s_shieldGroundShader.quadHalf >= 0)      SetShaderValue(gsh, s_shieldGroundShader.quadHalf,      &quadHalf,      SHADER_UNIFORM_FLOAT);

        float y = shObj->pos.y + 0.018f;
        rlSetTexture(s_shieldNoiseTex.id);
        rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(center.x - quadHalf, y, center.z - quadHalf);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(center.x - quadHalf, y, center.z + quadHalf);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(center.x + quadHalf, y, center.z + quadHalf);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(center.x + quadHalf, y, center.z - quadHalf);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetTexture(0);
    }

    if (started) {
        SkillManager_EndShader();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
    }
}

void VFX_ShieldShell_DrawRefraction(Camera3D cam)
{
    ShieldShell_InitShared();
    if (s_shieldShader.shader.id == 0) return;

    bool anyActive = false;
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
        if (s_shieldShells[i].active && s_shieldShells[i].level > 0.004f) { anyActive = true; break; }
    if (!anyActive) return;

    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 camUp   = Vector3CrossProduct(right, forward);

    for (int pass = 0; pass < 2; pass++) {
        bool isEmission = (pass == 1);
        VFXRenderScope scope = VFXRender_BeginDraw(
            isEmission ? VFX_RENDER_PASS_EMISSION : VFX_RENDER_PASS_BODY,
            isEmission ? VFX_SURFACE_PREMULTIPLIED : VFX_SURFACE_ALPHA, false);
        rlDrawRenderBatchActive();
        ShieldShell_DrawGroundPass(isEmission);
        SkillManager_BeginShader(s_shieldShader.shader);
        ShieldShell_BindSharedInputs(cam, right, camUp, forward);

        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        rlSetCullFace(RL_CULL_FACE_FRONT);
        ShieldShell_DrawPass(isEmission, -1.0f);
        rlDrawRenderBatchActive();

        rlSetCullFace(RL_CULL_FACE_BACK);
        ShieldShell_DrawPass(isEmission, 1.0f);
        rlDrawRenderBatchActive();
        SkillManager_EndShader();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

static void VC_ShieldShell_Draw3D(Camera3D cam)
{
    if (s_shieldDepthEnabled <= 0.5f) return;
    const float screenH = (float)GetScreenHeight();
    const float halfFovy = cam.fovy * 0.5f * DEG2RAD;
    bool hasBounds = false;
    Rectangle bounds = {0};
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i) {
        const VC_ShieldShell *sh = &s_shieldShells[i];
        if (!sh->active) continue;
        Vector3 centrePos = Shield_CenterForDesc(sh->pos, sh->radius, &sh->desc);
        float distance = Vector3Length(Vector3Subtract(centrePos, cam.position));
        if (distance <= 0.001f) continue;
        Vector2 centre = GetWorldToScreen(centrePos, cam);
        float radiusPx = sh->radius * screenH / (2.0f * distance * tanf(halfFovy));
        Rectangle r = { centre.x - radiusPx, centre.y - radiusPx, radiusPx * 2.0f, radiusPx * 2.0f };
        if (!hasBounds) { bounds = r; hasBounds = true; }
        else {
            float x1 = fmaxf(bounds.x + bounds.width, r.x + r.width);
            float y1 = fmaxf(bounds.y + bounds.height, r.y + r.height);
            bounds.x = fminf(bounds.x, r.x); bounds.y = fminf(bounds.y, r.y);
            bounds.width = x1 - bounds.x; bounds.height = y1 - bounds.y;
        }
    }
    if (hasBounds) SceneTargets_RequestSoftDepthRegion(SceneTargets_WindowRectToTarget(bounds));
}
