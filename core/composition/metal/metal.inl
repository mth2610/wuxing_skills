// metal.inl — Master include for metal element VFX sub-modules
// Included once by visual_composer.c

static CrystalDesc GetMetalBladeDesc(void)
{
    CrystalDesc desc = {0};
    desc.height = 1.1f;
    desc.radius = 0.11f;
    desc.taper = 0.95f; 
    desc.twist = 0.15f; 
    desc.noise = 0.03f; 
    desc.sides = 5;
    desc.segments = 4;
    return desc;
}

static Mesh GetMetalBladeTemplateMesh(void)
{
    static Mesh s_template = {0};
    static bool s_ready = false;
    if (!s_ready)
    {
        CrystalDesc desc = GetMetalBladeDesc();
        s_template = ProceduralMesh_BuildCrystalTemplateMesh(&desc);
        s_ready = true;
    }
    return s_template;
}

static CrystalMaterialInstanced GetMetalBladeMaterialInstanced(void)
{
    static CrystalMaterialInstanced s_metalMatI;
    static bool s_metalMatILoaded = false;
    if (!s_metalMatILoaded)
    {
        CrystalMaterialParams p = {0};
        p.baseColor = (Color){108, 120, 130, 255}; 
        p.edgeColor = (Color){245, 250, 255, 255}; 
        p.roughness = 0.82f;                       
        p.fresnel = 2.0f;                          
        p.refraction = 0.0f;                       
        p.sparkle = 1.0f;                          
        p.crack = 0.0f;                            
        p.emission = 0.06f;
        p.thickness = 0.6f;
        CrystalMaterialInstanced_Load(&s_metalMatI, &p);
        s_metalMatILoaded = true;
    }
    return s_metalMatI;
}

#define METAL_BLADE_COUNT 4

static ColorGradient s_steelGrad = {0};  
static ColorGradient s_steelHotGrad = {0}; 
static bool s_metalFxInit = false;

static void MetalFx_InitShared(void)
{
    if (s_metalFxInit)
        return;
    ColorGradient_AddStop(&s_steelGrad, 0.0f, (Color){210, 225, 235, 255});
    ColorGradient_AddStop(&s_steelGrad, 0.45f, (Color){149, 165, 166, 200});
    ColorGradient_AddStop(&s_steelGrad, 1.0f, (Color){60, 70, 75, 0});

    ColorGradient_AddStop(&s_steelHotGrad, 0.0f, (Color){255, 255, 250, 255});
    ColorGradient_AddStop(&s_steelHotGrad, 0.3f, (Color){230, 245, 255, 230});
    ColorGradient_AddStop(&s_steelHotGrad, 1.0f, (Color){120, 150, 170, 0});
    s_metalFxInit = true;
}

#include "metal_shard_cluster.inl"
#include "blade_ring.inl"
#include "blade_storm.inl"
#include "shrapnel_burst.inl"
#include "ricochet_spark.inl"
#include "vc_meteor_comet_test.inl"
// @gen:metal_includes begin
// @gen:metal_includes end
