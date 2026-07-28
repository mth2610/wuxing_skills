// water.inl — Master include for water element VFX sub-modules
// Included once by visual_composer.c

static Shader s_waterTubeShader = {0};
static int s_waterTimeLoc = -1;
static int s_waterViewPosLoc = -1;
static int s_waterLightDirLoc = -1;
static int s_waterUvLengthLoc = -1;

static void InitWaterTubeShaderIfNeeded(void)
{
    if (s_waterTubeShader.id == 0)
    {
        s_waterTubeShader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
        s_waterTimeLoc = GetShaderLocation(s_waterTubeShader, "u_time");
        s_waterViewPosLoc = GetShaderLocation(s_waterTubeShader, "viewPos");
        s_waterLightDirLoc = GetShaderLocation(s_waterTubeShader, "u_lightDir");
        s_waterUvLengthLoc = GetShaderLocation(s_waterTubeShader, "u_uvLength");
    }
}

static CrystalMaterialParams GetIceCrystalMaterialParams(void)
{
    CrystalMaterialParams p = {0};
    p.baseColor = (Color){0, 110, 230, 200};
    p.edgeColor = (Color){130, 220, 255, 255};
    p.roughness = 0.35f;
    p.fresnel = 0.6f;
    p.refraction = 0.15f;
    p.sparkle = 0.2f;
    p.crack = 0.2f;
    p.emission = 0.0f;
    p.thickness = 1.8f;
    return p;
}

static CrystalMaterial GetIceCrystalMaterial(void)
{
    static CrystalMaterial s_iceMat;
    static bool s_iceMatLoaded = false;
    if (!s_iceMatLoaded)
    {
        CrystalMaterialParams p = GetIceCrystalMaterialParams();
        CrystalMaterial_Load(&s_iceMat, &p);
        s_iceMatLoaded = true;
    }
    return s_iceMat;
}

static CrystalMaterialInstanced GetIceCrystalMaterialInstanced(void)
{
    static CrystalMaterialInstanced s_iceMatI;
    static bool s_iceMatILoaded = false;
    if (!s_iceMatILoaded)
    {
        CrystalMaterialParams p = GetIceCrystalMaterialParams();
        CrystalMaterialInstanced_Load(&s_iceMatI, &p);
        s_iceMatILoaded = true;
    }
    return s_iceMatI;
}

static CrystalDesc GetIceCrystalDesc(void)
{
    CrystalDesc desc = {0};
    desc.height = 1.3f;
    desc.radius = 0.16f;
    desc.taper = 0.85f;
    desc.twist = 0.6f;
    desc.noise = 0.08f;
    desc.sides = 6;
    desc.segments = 6;
    return desc;
}

static Mesh GetIceCrystalTemplateMesh(void)
{
    static Mesh s_template = {0};
    static bool s_ready = false;
    if (!s_ready)
    {
        CrystalDesc desc = GetIceCrystalDesc();
        s_template = ProceduralMesh_BuildCrystalTemplateMesh(&desc);
        s_ready = true;
    }
    return s_template;
}

static ColorGradient s_dropGrad = {0};    
static ColorGradient s_dropCapGrad = {0}; 
static ColorGradient s_mistGrad = {0};    
static ColorGradient s_bubbleGrad = {0};  
static SkillCurve s_mistShape = {0};      
static SkillCurve s_softInOut = {0};      
static bool s_waterFxInit = false;

static void WaterFx_InitShared(void)
{
    if (s_waterFxInit)
        return;
    ColorGradient_AddStop(&s_dropGrad, 0.0f, (Color){120, 200, 255, 240});
    ColorGradient_AddStop(&s_dropGrad, 0.5f, (Color){41, 128, 185, 200});
    ColorGradient_AddStop(&s_dropGrad, 1.0f, (Color){15, 60, 110, 0});

    ColorGradient_AddStop(&s_dropCapGrad, 0.0f, (Color){235, 250, 255, 255});
    ColorGradient_AddStop(&s_dropCapGrad, 0.4f, (Color){170, 225, 255, 220});
    ColorGradient_AddStop(&s_dropCapGrad, 1.0f, (Color){60, 130, 190, 0});

    ColorGradient_AddStop(&s_mistGrad, 0.0f, (Color){150, 190, 215, 0});
    ColorGradient_AddStop(&s_mistGrad, 0.3f, (Color){140, 180, 205, 90});
    ColorGradient_AddStop(&s_mistGrad, 1.0f, (Color){100, 130, 155, 0});

    ColorGradient_AddStop(&s_bubbleGrad, 0.0f, (Color){140, 210, 255, 0});
    ColorGradient_AddStop(&s_bubbleGrad, 0.2f, (Color){170, 230, 255, 150});
    ColorGradient_AddStop(&s_bubbleGrad, 0.85f, (Color){200, 245, 255, 170});
    ColorGradient_AddStop(&s_bubbleGrad, 1.0f, (Color){255, 255, 255, 0});

    FloatCurve_AddStop(&s_mistShape, 0.0f, 0.5f);
    FloatCurve_AddStop(&s_mistShape, 1.0f, 1.7f);

    FloatCurve_AddStop(&s_softInOut, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_softInOut, 0.2f, 1.0f);
    FloatCurve_AddStop(&s_softInOut, 0.75f, 0.9f);
    FloatCurve_AddStop(&s_softInOut, 1.0f, 0.0f);

    s_waterFxInit = true;
}

#include "ice_crystal.inl"
// @gen:water_includes begin
// 1 include(s) — auto-managed by sync_vfx_test.py
#include "water_stream.inl"
// @gen:water_includes end
