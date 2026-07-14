// earth.inl — Master include for earth element VFX sub-modules
// Included once by visual_composer.c

static ColorGradient s_earthDustGrad = {0};  
static ColorGradient s_earthChunkGrad = {0}; 
static ColorGradient s_earthGrainGrad = {0}; 
static SkillCurve s_earthDustBillow = {0};   
static bool s_earthFxInit = false;

static void EarthFx_InitShared(void)
{
    if (s_earthFxInit)
        return;
    ColorGradient_AddStop(&s_earthDustGrad, 0.0f, (Color){150, 125, 95, 0});
    ColorGradient_AddStop(&s_earthDustGrad, 0.25f, (Color){140, 115, 88, 120});
    ColorGradient_AddStop(&s_earthDustGrad, 1.0f, (Color){85, 72, 60, 0});

    ColorGradient_AddStop(&s_earthChunkGrad, 0.0f, (Color){175, 130, 85, 255});
    ColorGradient_AddStop(&s_earthChunkGrad, 0.6f, (Color){120, 90, 60, 220});
    ColorGradient_AddStop(&s_earthChunkGrad, 1.0f, (Color){55, 42, 32, 0});

    ColorGradient_AddStop(&s_earthGrainGrad, 0.0f, (Color){235, 200, 150, 255});
    ColorGradient_AddStop(&s_earthGrainGrad, 0.5f, (Color){190, 150, 100, 200});
    ColorGradient_AddStop(&s_earthGrainGrad, 1.0f, (Color){100, 80, 55, 0});

    FloatCurve_AddStop(&s_earthDustBillow, 0.0f, 0.45f);
    FloatCurve_AddStop(&s_earthDustBillow, 1.0f, 1.8f);
    s_earthFxInit = true;
}

static ForceField s_earthGravFld = {0};
static ForceField *EarthGravField(void)
{
    if (s_earthGravFld.layerCount == 0)
        ForceField_AddLayer(&s_earthGravFld, (ForceLayer){
                                                 .type = FORCE_GRAVITY_DIR,
                                                 .direction = (Vector3){0.0f, -1.0f, 0.0f},
                                                 .strength = 9.8f});
    return &s_earthGravFld;
}

static Mesh GetFloatingStoneTemplateMesh(void)
{
    static Mesh s_template = {0};
    static bool s_ready = false;
    if (!s_ready)
    {
        s_template = ProceduralMesh_BuildRockTemplateMesh(1.0f, 0.5f, 733, 1);
        s_ready = true;
    }
    return s_template;
}

static EffectMaterialInstanced GetFloatingStoneMaterialInstanced(void)
{
    static EffectMaterialInstanced s_rockMatI;
    static bool s_rockMatILoaded = false;
    if (!s_rockMatILoaded)
    {
        EffectMaterial nonInstanced;
        Material_Get(&nonInstanced, MAT_ROCK);
        EffectMaterialInstanced_Load(&s_rockMatI, &nonInstanced.params);
        s_rockMatILoaded = true;
    }
    return s_rockMatI;
}

#include "stone_pillar.inl"
#include "boulder.inl"
#include "fissure_streak.inl"
#include "rock_burst.inl"
#include "floating_stones.inl"
#include "quake_rumble.inl"
