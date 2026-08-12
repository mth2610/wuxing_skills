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

/* --- SSF liquids ---------------------------------------------------------
 *
 * The screen-space fluid surface takes a FluidLiquidDesc, not a colour triple:
 * which branch of the optics runs (dielectric / emissive / conductor) is not
 * something a colour can express. These four build one from the element
 * material table so a liquid stays recognisable as its element.
 *
 * Every constant below is stated against FLUID_REFERENCE_DEPTH_M (0.20 m), the
 * one scale core/fluid/shaders/fluid_surface.fs expresses its optics in. */
static FluidLiquidDesc VFX_LiquidWater(void)
{
    const VFX_ElementMaterial *m = VFX_Material(VC_MAT_WATER);
    return FluidSurface_DielectricDesc(m->body, m->glow, m->soft);
}

/* Poison needs no code of its own — it is water's optics with green absorption,
 * which Beer-Lambert already derives from the body colour. Kept as a named
 * preset only so callers do not have to know that. */
static FluidLiquidDesc VFX_LiquidPoison(void)
{
    const VFX_ElementMaterial *m = VFX_Material(VC_MAT_POISON);
    return FluidSurface_DielectricDesc(m->body, m->glow, m->soft);
}

static FluidLiquidDesc VFX_LiquidLava(void)
{
    const VFX_ElementMaterial *m = VFX_Material(VC_MAT_FIRE);
    /* The hot core is an authored INCANDESCENT colour, not VC_Whiten(glow).
     * Whitening lifts every channel equally, so fire's (255,90,20) becomes
     * (255,164,126) — a pale PEACH, because the blue it added is what the eye
     * reads as "washed out". Measured on the bench: the body underneath was a
     * rich crimson the whole time (the `water - emission` view proved it) and
     * the peach was entirely this colour. A blackbody gets brighter by climbing
     * red -> orange -> yellow -> white, so the core goes yellow and only reaches
     * white where the tonemap clips it.
     *
     * Measured off the render rather than guessed: the molten regions came out
     * at (242,187,87), G/R = 0.77 — which is just this constant, so the
     * pipeline was not washing anything out and (255,200,70) was simply too
     * YELLOW. Molten rock sits near G/R 0.5. */
    FluidLiquidDesc d = FluidSurface_DielectricDesc(m->body, (Color){255, 140, 30, 255}, m->soft);
    d.liquidClass = FLUID_LIQUID_EMISSIVE;
    /* Above 1.0 on purpose — lava should survive the HDR tonemap as a light
     * source, not as a bright surface — but only just. At 2.4 every channel of
     * the whitened core clipped and the body rendered pale peach; the crimson
     * skin that makes it read as lava was there underneath and was simply being
     * buried.
     *
     * 1.6 was still too hot once the crust started driving temperature: the
     * molten regions pushed BOTH red and green past 1.0, so the tonemap
     * flattened them into pale yellow plateaus with no internal variation —
     * hard-edged patches that read as cracked eggshell rather than as
     * something molten. 1.1 keeps the body just under the clip so it holds a
     * gradient, and only the hottest seams blow out. */
    d.emission = 1.1f;
    /* Molten silicate, roughly. Also raises F0 from water's 2% to 5.5%, which is
     * most of why a lava rim reads harder than a water one. */
    d.ior = 1.60f;
    /* A crust is not a mirror. */
    d.roughnessScale = 3.2f;
    /* The colour alone CANNOT make it opaque: the body is saturated, so its red
     * channel transmits ~100% and the background reads straight through the
     * middle of the body. 24/m puts one reference depth at e^-4.8 — background
     * gone, and a genuinely thin sheet still translucent. */
    d.opacityPerMetre = 24.0f;
    d.foam = 1.0f;   /* re-read as crust by the emissive branch */
    return d;
}

static FluidLiquidDesc VFX_LiquidMetal(void)
{
    const VFX_ElementMaterial *m = VFX_Material(VC_MAT_METAL);
    /* A conductor's `body` IS its F0 — the fraction it reflects at normal
     * incidence, per channel. Silver-grey at 149/165/166 is 0.58/0.65/0.65,
     * which is a believable liquid metal (mercury sits near 0.78, aluminium
     * 0.91) and stays recognisably the METAL element rather than a mirror. */
    FluidLiquidDesc d = FluidSurface_DielectricDesc(m->body, m->glow, m->soft);
    d.liquidClass = FLUID_LIQUID_CONDUCTOR;
    /* Sharper than water, not blurrier. 1.6 was chosen to hide the
     * reconstruction's normal wobble, but a broad lobe over a flat environment
     * is exactly what plastic looks like — it smears away the one high-contrast
     * feature (the sun) that says "mirror". The wobble it was hiding reads as
     * a liquid metal surface RIPPLING, which is what this material is. */
    d.roughnessScale = 0.65f;
    /* No transmission at all, so this only bounds the in-scatter the dielectric
     * assembly would have produced; the conductor branch discards it anyway. */
    d.opacityPerMetre = 60.0f;
    d.foam = 0.0f;   /* a conductor has no foam */
    return d;
}

#include "ice_crystal.inl"
// @gen:water_includes begin
// 5 include(s) — auto-managed by sync_vfx_test.py
#include "water_stream.inl"
#include "fluid_impact_test.inl"
#include "water_orb.inl"
#include "water_ring.inl"
#include "liquid_bench.inl"
// @gen:water_includes end
