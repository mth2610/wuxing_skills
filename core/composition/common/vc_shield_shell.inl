// ShieldShell — glass-like force-field volume. The scene colour comes from the
// safe post-3D snapshot (never from the live render target), while the packed
// field and additive crest provide the authored magical identity.
#include "core/tuning.h"
#include "core/gfx_quality.h"

#define VFX_SHIELD_SHELL_MAX 8
// TESSELLATION: TWO budgets, and they are not the same budget.
//
// 14x14 = 392 triangles was chosen for the 200-400 mobile force-field budget, and it
// holds that budget — but at a normal gameplay apparent size a 14-slice sphere does not
// read as a sphere: its silhouette is a visible 14-sided polygon, which is the first
// thing the eye finds on a bright background (measured capture:
// autotest_output/vfx_matrix/idx21/white_w90.png). A shell whose outline is a polygon
// is not glass at any coverage.
//
//   PER-SHELL, mobile: 200-400 triangles. This is what 14x14 = 392 was chosen for, and
//   LOW is capped there and never scales up — on mobile the constraint binds per shell.
//   AGGREGATE: SHIELD_MAX * rings * slices * 2 <= 6400, which core/tests/shield_shell_test.c
//   guards. That is a budget for EIGHT simultaneous shields; one shield alone leaves seven
//   eighths of it unspent, and a lone shell is exactly when it is large on screen and its
//   facets show. So above LOW the tier sets a ceiling and the LIVE COUNT spends what the
//   budget actually has, clamping back down as shields multiply. The eight-shield worst
//   case is unchanged.
#define SHIELD_SHELL_TRI_BUDGET 6400
#define SHIELD_SHELL_RINGS_LOW 14      /* hard cap: the mobile per-shell budget */
#define SHIELD_SHELL_RINGS_MED 24
#define SHIELD_SHELL_RINGS_HIGH 40

// The 6400-triangle cap is a budget for EIGHT simultaneous shields. One shield alone
// leaves seven eighths of it unspent, and a lone shell is exactly when its silhouette is
// large on screen and its facets are most visible. So the tier sets a ceiling and the
// live count spends what the budget actually has:
//
//     activeCount * rings * rings * 2 <= 6400
//
// The worst case is therefore identical to before (8 shields -> 20x20 at HIGH, 14x14 at
// LOW), while a single shell gets a round outline for free. Floored at the LOW value so
// no configuration is ever coarser than what already shipped.
static int ShieldShell_Rings(int activeCount)
{
    GfxQuality q = GfxQuality_Get();
    int ceiling = (q >= GFX_HIGH) ? SHIELD_SHELL_RINGS_HIGH
                : (q >= GFX_MED)  ? SHIELD_SHELL_RINGS_MED
                                  : SHIELD_SHELL_RINGS_LOW;
    if (q <= GFX_LOW) return SHIELD_SHELL_RINGS_LOW;   /* mobile: per-shell budget binds */
    if (activeCount < 1) activeCount = 1;
    int rings = SHIELD_SHELL_RINGS_LOW;
    while (rings + 2 <= ceiling &&
           activeCount * (rings + 2) * (rings + 2) * 2 <= SHIELD_SHELL_TRI_BUDGET)
        rings += 2;
    return rings;
}

typedef struct {
    bool active, stopping;
    Vector3 pos;
    VC_MaterialId mat;
    Texture2D packedMap;
    Texture2D flowMap;
    Texture2D matcapMap;
    Vector3 impactWorld;
    float impactAge;
    float radius, target, level, elapsed;
} VC_ShieldShell;

typedef struct {
    Shader shader;
    int bodyColor, rimColor, opacity, rimStrength, rimPower, emissionOnly;
    int bodyOpacity, emissionGain;
    int lightDirView, packedTex, hasPacked, flowTex, hasFlow, matcapTex, hasMatcap;
    int depthTex, hasDepth, depthEnabled, depthLod;
    int sceneTex, hasScene;
    int noiseScale, noiseSpeed;
    int impactView, impactAge, rippleFrequency, rippleSpeed;
    int flowStrength, flowSpeed, parallaxDepth, innerDepth;
    int contactStrength, contactColor, contactThickness;
    int baseAlpha, fresnelAlpha, contactAlpha;
} VC_ShieldShader;

static VC_ShieldShell s_shieldShells[VFX_SHIELD_SHELL_MAX];
static VC_ShieldShader s_shieldShader = {0};
static bool s_shieldInit = false;
static float s_shieldOpacity = 1.0f;
static float s_shieldRim = 2.0f;
static float s_shieldRimPower = 8.0f;   // exponent on (1-|N.V|) — the rim band's WIDTH
// Refraction + contact payload (recipe):
//   distortion = noise * strength | alpha = base + fresnel*A + contact*B
static float s_shieldNoiseScale = 28.0f;
static float s_shieldNoiseSpeed = 2.2f;       // noise scroll speed
static float s_shieldContact = 1.0f;          // contact glow strength
static float s_shieldContactThickness = 0.35f; // meters of depth gap = "touching"
static float s_shieldBaseAlpha = 0.025f;
static float s_shieldFresnelAlpha = 0.18f;
static float s_shieldContactAlpha = 0.42f;
// ON by default since 17/08/2026. It was off because the feature did not work — the
// depth region was requested from the post-3D draw, where SceneTargets_Begin() had
// already cleared the validity flag, so the snapshot never ran and depthContact()
// returned 0 for every fragment. With the request moved into the 3D pass it draws the
// shell's real ground intersection, which is the whole point of a shield that sits on
// terrain. Set `shield_shell_depth_enabled = 0` in tuning.cfg to go back.
static float s_shieldDepthEnabled = 1.0f;
static float s_shieldDepthLod = 0.5f;
static float s_shieldRippleFrequency = 18.0f;
static float s_shieldRippleSpeed = 8.0f;
static float s_shieldFlowStrength = 0.08f;
static float s_shieldFlowSpeed = 0.35f;
static float s_shieldParallaxDepth = 0.045f;
static float s_shieldInnerDepth = 0.70f;
static Vector3 s_shieldCameraPosition;
static Vector3 s_shieldCameraRight, s_shieldCameraUp, s_shieldCameraForward;

static float ShieldShell_Clamp01(float value)
{ return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

static void ShieldShell_InitShared(void)
{
    if (s_shieldInit) return;

    s_shieldShader.shader = ResourceManager_LoadShader(
        "core/shaders/glass_shell.vs", "core/shaders/glass_shell.fs");
    TraceLog(LOG_INFO, "SHIELDSHELL_RENDER rev=glass-volume-alpha35-rear45-body135");
    s_shieldShader.bodyColor = GetShaderLocation(s_shieldShader.shader, "u_bodyColor");
    s_shieldShader.rimColor = GetShaderLocation(s_shieldShader.shader, "u_rimColor");
    s_shieldShader.opacity = GetShaderLocation(s_shieldShader.shader, "u_opacity");
    s_shieldShader.rimStrength = GetShaderLocation(s_shieldShader.shader, "u_rimStrength");
    s_shieldShader.rimPower = GetShaderLocation(s_shieldShader.shader, "u_rimPower");
    s_shieldShader.bodyOpacity = GetShaderLocation(s_shieldShader.shader, "u_bodyOpacity");
    s_shieldShader.emissionGain = GetShaderLocation(s_shieldShader.shader, "u_emissionGain");
    s_shieldShader.emissionOnly = GetShaderLocation(s_shieldShader.shader, "u_emissionOnly");
    s_shieldShader.lightDirView = GetShaderLocation(s_shieldShader.shader, "u_lightDirView");
    s_shieldShader.packedTex = GetShaderLocation(s_shieldShader.shader, "u_packedTex");
    s_shieldShader.hasPacked = GetShaderLocation(s_shieldShader.shader, "u_hasPacked");
    s_shieldShader.flowTex = GetShaderLocation(s_shieldShader.shader, "u_flowTex");
    s_shieldShader.hasFlow = GetShaderLocation(s_shieldShader.shader, "u_hasFlow");
    s_shieldShader.matcapTex = GetShaderLocation(s_shieldShader.shader, "u_matcapTex");
    s_shieldShader.hasMatcap = GetShaderLocation(s_shieldShader.shader, "u_hasMatcap");
    s_shieldShader.depthTex = GetShaderLocation(s_shieldShader.shader, "u_depthTex");
    s_shieldShader.hasDepth = GetShaderLocation(s_shieldShader.shader, "u_hasDepth");
    s_shieldShader.depthEnabled = GetShaderLocation(s_shieldShader.shader, "u_depthEnabled");
    s_shieldShader.depthLod = GetShaderLocation(s_shieldShader.shader, "u_depthLod");
    s_shieldShader.noiseScale = GetShaderLocation(s_shieldShader.shader, "u_noiseScale");
    s_shieldShader.noiseSpeed = GetShaderLocation(s_shieldShader.shader, "u_noiseSpeed");
    s_shieldShader.contactStrength = GetShaderLocation(s_shieldShader.shader, "u_contactStrength");
    s_shieldShader.contactColor = GetShaderLocation(s_shieldShader.shader, "u_contactColor");
    s_shieldShader.contactThickness = GetShaderLocation(s_shieldShader.shader, "u_contactThickness");
    s_shieldShader.baseAlpha = GetShaderLocation(s_shieldShader.shader, "u_baseAlpha");
    s_shieldShader.fresnelAlpha = GetShaderLocation(s_shieldShader.shader, "u_fresnelAlpha");
    s_shieldShader.contactAlpha = GetShaderLocation(s_shieldShader.shader, "u_contactAlpha");
    s_shieldShader.impactView = GetShaderLocation(s_shieldShader.shader, "u_impactView");
    s_shieldShader.impactAge = GetShaderLocation(s_shieldShader.shader, "u_impactAge");
    s_shieldShader.rippleFrequency = GetShaderLocation(s_shieldShader.shader, "u_rippleFrequency");
    s_shieldShader.rippleSpeed = GetShaderLocation(s_shieldShader.shader, "u_rippleSpeed");
    s_shieldShader.flowStrength = GetShaderLocation(s_shieldShader.shader, "u_flowStrength");
    s_shieldShader.flowSpeed = GetShaderLocation(s_shieldShader.shader, "u_flowSpeed");
    s_shieldShader.parallaxDepth = GetShaderLocation(s_shieldShader.shader, "u_parallaxDepth");
    s_shieldShader.innerDepth = GetShaderLocation(s_shieldShader.shader, "u_innerDepth");
    s_shieldShader.sceneTex = GetShaderLocation(s_shieldShader.shader, "u_sceneTex");
    s_shieldShader.hasScene = GetShaderLocation(s_shieldShader.shader, "u_hasScene");

    /* ShieldShell is a translucent carrier: keep its mass below the bright
     * background while letting the rim/emission carry the silhouette. */
    Tuning_RegisterFloat("shield_shell_opacity", &s_shieldOpacity, 0.78f);
    /* 2.15 -> 2.0, chosen by measurement alongside rim_power: at this pair the silhouette
       band and the ground-contact line come out the same width (9 px each) with the
       silhouette a little brighter (peak 247 vs 228), which is the ordering the shapes
       should have. It went to 3.0 briefly to compensate for a narrower band, back down
       once the white core returned to `wallDensity` and stopped costing luminance. */
    Tuning_RegisterFloat("shield_shell_rim", &s_shieldRim, 2.0f);
    /* Rim WIDTH, the counterpart of shield_shell_contact_thickness. Higher = narrower.
       The default is measured, not chosen: it is the exponent at which the silhouette
       band's screen width matches the ground-contact line's. */
    Tuning_RegisterFloat("shield_shell_rim_power", &s_shieldRimPower, 8.0f);
    Tuning_RegisterFloat("shield_shell_noise_scale", &s_shieldNoiseScale, 28.0f);
    Tuning_RegisterFloat("shield_shell_noise_speed", &s_shieldNoiseSpeed, 2.2f);
    Tuning_RegisterFloat("shield_shell_contact", &s_shieldContact, 1.0f);
    Tuning_RegisterFloat("shield_shell_contact_thickness", &s_shieldContactThickness, 0.35f);
    Tuning_RegisterFloat("shield_shell_base_alpha", &s_shieldBaseAlpha, 0.025f);
    Tuning_RegisterFloat("shield_shell_fresnel_alpha", &s_shieldFresnelAlpha, 0.34f);
    Tuning_RegisterFloat("shield_shell_contact_alpha", &s_shieldContactAlpha, 0.70f);
    /* Registration is what actually sets the runtime value (Tuning_RegisterFloat assigns
       the default), so this number, not the static initialiser above, is the shipped one. */
    Tuning_RegisterFloat("shield_shell_depth_enabled", &s_shieldDepthEnabled, 1.0f);
    Tuning_RegisterFloat("shield_shell_depth_lod", &s_shieldDepthLod, 0.5f);
    Tuning_RegisterFloat("shield_shell_ripple_frequency", &s_shieldRippleFrequency, 18.0f);
    Tuning_RegisterFloat("shield_shell_ripple_speed", &s_shieldRippleSpeed, 8.0f);
    Tuning_RegisterFloat("shield_shell_flow_strength", &s_shieldFlowStrength, 0.08f);
    Tuning_RegisterFloat("shield_shell_flow_speed", &s_shieldFlowSpeed, 0.35f);
    Tuning_RegisterFloat("shield_shell_parallax_depth", &s_shieldParallaxDepth, 0.045f);
    Tuning_RegisterFloat("shield_shell_inner_depth", &s_shieldInnerDepth, 0.70f);
    s_shieldInit = true;
}

int VFX_ShieldShell_SpawnEx(Vector3 pos, VC_MaterialId mat, float radius,
                             float intensity, const VFX_ShieldSurface *surface)
{
    ShieldShell_InitShared();
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        if (s_shieldShells[i].active) continue;
        s_shieldShells[i] = (VC_ShieldShell){
            .active = true, .stopping = false, .pos = pos, .mat = mat,
            .radius = radius > 0.0f ? radius : 0.9f,
            .target = ShieldShell_Clamp01(intensity), .level = 0.0f, .elapsed = 0.0f,
            .packedMap = surface ? (surface->packedMap.id ? surface->packedMap : surface->body) : (Texture2D){0},
            .flowMap = surface ? surface->flowMap : (Texture2D){0},
            .matcapMap = surface ? surface->matcapMap : (Texture2D){0},
            .impactAge = 1000000.0f
        };
        return i;
    }
    return -1;
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
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
    {
        s_shieldShells[handle].target = ShieldShell_Clamp01(intensity01);
        s_shieldShells[handle].stopping = false;
    }
}

void VFX_ShieldShell_SetSurface(int handle, const VFX_ShieldSurface *surface)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active && surface)
    {
        s_shieldShells[handle].packedMap = surface->packedMap.id ? surface->packedMap : surface->body;
        s_shieldShells[handle].flowMap = surface->flowMap;
        s_shieldShells[handle].matcapMap = surface->matcapMap;
    }
}

void VFX_ShieldShell_SetImpact(int handle, Vector3 impactWorld, float timeSinceImpact)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active) {
        s_shieldShells[handle].impactWorld = impactWorld;
        s_shieldShells[handle].impactAge = timeSinceImpact < 0.0f ? 0.0f : timeSinceImpact;
    }
}

void VFX_ShieldShell_Stop(int handle)
{
    if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX && s_shieldShells[handle].active)
    {
        s_shieldShells[handle].stopping = true;
        s_shieldShells[handle].target = 0.0f;
    }
}

void VFX_KillShieldShell(int handle)
{ if (handle >= 0 && handle < VFX_SHIELD_SHELL_MAX) s_shieldShells[handle].active = false; }

static void VC_ShieldShell_Update(float dt)
{
    bool anyActive = false;
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active) continue;
        anyActive = true;
        shield->elapsed += dt;
        shield->level += (shield->target - shield->level) * (1.0f - expf(-dt * 7.0f));
        if (shield->stopping && shield->level < 0.004f)
        {
            shield->active = false;
            continue;
        }
    }
    if (anyActive) SceneTargets_RequestSceneSnapshot();
}

// Bind the refraction payload once per pass (inside BeginShaderMode): the
// sample-safe scene copy and the previous-frame linear depth. When a source
// is missing (first frame, no depth-texture path) the matching u_has* gate
// turns the term off in the shader, so nothing samples garbage.
static void ShieldShell_BindInputs(Camera3D cam)
{
    Texture2D sceneTex = SceneTargets_GetSceneSnapshotTexture();
    int hasScene = sceneTex.id != 0 ? 1 : 0;
    SetShaderValue(s_shieldShader.shader, s_shieldShader.hasScene,
                   &hasScene, SHADER_UNIFORM_INT);
    if (hasScene && s_shieldShader.sceneTex >= 0)
        SetShaderValueTexture(s_shieldShader.shader, s_shieldShader.sceneTex, sceneTex);
    Texture2D depthTex = SceneTargets_GetDepthTexture();
    int hasDepth = (depthTex.id != 0 && s_shieldDepthEnabled > 0.5f) ? 1 : 0;
    SetShaderValue(s_shieldShader.shader, s_shieldShader.hasDepth, &hasDepth, SHADER_UNIFORM_INT);
    if (hasDepth && s_shieldShader.depthTex >= 0)
        SetShaderValueTexture(s_shieldShader.shader, s_shieldShader.depthTex, depthTex);
    SetShaderValue(s_shieldShader.shader, s_shieldShader.depthEnabled, &s_shieldDepthEnabled, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shieldShader.shader, s_shieldShader.depthLod, &s_shieldDepthLod, SHADER_UNIFORM_FLOAT);
}

/* `pos` is the shell's GROUND POINT, not its centre: the sphere is lifted by half
 * its radius, which leaves three quarters of it above the plane through `pos` and
 * one quarter buried.
 *
 * The arithmetic, so the next change to it is not guesswork: for radius r and
 * centre height h the fraction above ground is (h + r) / 2r. Setting that to 3/4
 * gives h = r/2, and the lowest point lands at -r/2 — a quarter of the sphere's
 * 2r height. A full-radius lift (h = r) is the tangent case, 100% above.
 *
 * It used to be the centre, and every caller passed a character's feet — so half
 * the bubble was underground. The shell draws after the 3D pass, against a depth
 * buffer that already holds the floor, so the floor correctly rejected the FAR
 * wall's lower half while the NEAR wall's lower half (in front of the floor)
 * survived. What that renders as is a dome with a hard bright ellipse across the
 * middle: the ellipse is the sphere meeting the floor, and the missing far wall
 * is why the lower body looked hollow. Bisected by forcing alpha to 1 and drawing
 * the far wall alone — still a crescent, so neither the shader nor draw order nor
 * winding was responsible.
 *
 * Resolved here rather than at the call site because a caller that forgets the
 * lift reproduces exactly this, and nothing about the result says "you passed the
 * wrong height". */
/* Centre height as a multiple of the radius: 0.5 => three quarters above
 * ground. 1.0 would make the sphere tangent to it. */
/* Both shells live in the same translation unit (visual_composer.c pulls every
 * .inl in), so this is guarded rather than defined twice. */
#ifndef SHIELD_BURIED_LIFT
#define SHIELD_BURIED_LIFT 0.5f
#endif

static Vector3 ShieldCentre(Vector3 groundPos, float radius)
{
    groundPos.y += radius * SHIELD_BURIED_LIFT;
    return groundPos;
}

static void ShieldShell_DrawPass(bool emissionOnly)
{
    int activeCount = 0;
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
        if (s_shieldShells[i].active) activeCount++;
    const int rings = ShieldShell_Rings(activeCount);
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        VC_ShieldShell *shield = &s_shieldShells[i];
        if (!shield->active || shield->level <= 0.004f) continue;

        const VFX_ElementMaterial *material = VFX_Material(shield->mat);
        Vector4 bodyColor = ColorNormalize(material->body);
        Vector4 rimColor = ColorNormalize(material->glow);
        float pulse = VC_Breathe(shield->elapsed, 1.15f, 0.012f);
        float radius = shield->radius * (0.99f + 0.01f * pulse);
        VFXResolvedAppearance appearance = VFXAppearance_Resolve(
            VFX_APPEARANCE_MAGIC,
            (VFXResolvedAppearance){ VFX_SURFACE_ALPHA, VFX_CONTRAST_MAGIC,
                                     1.0f, 1.0f, 0.78f, true });
        /* The glass centre is made transparent by its spatial coverage, not
         * by starving the complete carrier.  This preserves a readable
         * membrane through the middle-to-edge gradient. */
        float bodyCoverage = appearance.bodyOpacity * 4.0f;
        float opacity = ShieldShell_Clamp01(
            shield->level * s_shieldOpacity *
            (emissionOnly ? appearance.emissionIntensity : bodyCoverage));
        int emission = emissionOnly ? 1 : 0;
        int hasPacked = shield->packedMap.id != 0 ? 1 : 0;
        int hasFlow = shield->flowMap.id != 0 ? 1 : 0;
        int hasMatcap = shield->matcapMap.id != 0 ? 1 : 0;
        Vector3 delta = Vector3Subtract(shield->impactWorld, s_shieldCameraPosition);
        Vector3 impactView = { Vector3DotProduct(delta, s_shieldCameraRight),
                               Vector3DotProduct(delta, s_shieldCameraUp),
                               -Vector3DotProduct(delta, s_shieldCameraForward) };

        SetShaderValue(s_shieldShader.shader, s_shieldShader.bodyColor,
                       &bodyColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rimColor,
                       &rimColor, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.opacity,
                       &opacity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.hasPacked,
                       &hasPacked, SHADER_UNIFORM_INT);
        if (hasPacked) SetShaderValueTexture(s_shieldShader.shader,
                                             s_shieldShader.packedTex, shield->packedMap);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.hasFlow,
                       &hasFlow, SHADER_UNIFORM_INT);
        if (hasFlow) SetShaderValueTexture(s_shieldShader.shader,
                                            s_shieldShader.flowTex, shield->flowMap);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.hasMatcap,
                       &hasMatcap, SHADER_UNIFORM_INT);
        if (hasMatcap) SetShaderValueTexture(s_shieldShader.shader,
                                              s_shieldShader.matcapTex, shield->matcapMap);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rimStrength,
                       &s_shieldRim, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rimPower,
                       &s_shieldRimPower, SHADER_UNIFORM_FLOAT);
        float bodyOpacity = emissionOnly ? 0.0f : bodyCoverage;
        float emissionGain = emissionOnly ? appearance.emissionIntensity : 0.0f;
        SetShaderValue(s_shieldShader.shader, s_shieldShader.bodyOpacity,
                       &bodyOpacity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.emissionGain,
                       &emissionGain, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.emissionOnly,
                       &emission, SHADER_UNIFORM_INT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.noiseScale,
                       &s_shieldNoiseScale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.noiseSpeed,
                       &s_shieldNoiseSpeed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.contactStrength,
                       &s_shieldContact, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.contactThickness,
                       &s_shieldContactThickness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.baseAlpha,
                       &s_shieldBaseAlpha, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.fresnelAlpha,
                       &s_shieldFresnelAlpha, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.contactAlpha,
                       &s_shieldContactAlpha, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.impactView,
                       &impactView, SHADER_UNIFORM_VEC3);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.impactAge,
                       &shield->impactAge, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rippleFrequency,
                       &s_shieldRippleFrequency, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.rippleSpeed,
                       &s_shieldRippleSpeed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.flowStrength,
                       &s_shieldFlowStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.flowSpeed,
                       &s_shieldFlowSpeed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.parallaxDepth,
                       &s_shieldParallaxDepth, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_shieldShader.shader, s_shieldShader.innerDepth,
                       &s_shieldInnerDepth, SHADER_UNIFORM_FLOAT);
        // Contact glow reads as a hotter, near-white band of the element hue.
        Vector4 contactColor = {
            rimColor.x + (1.0f - rimColor.x) * 0.30f,
            rimColor.y + (1.0f - rimColor.y) * 0.30f,
            rimColor.z + (1.0f - rimColor.z) * 0.30f,
            1.0f
        };
        SetShaderValue(s_shieldShader.shader, s_shieldShader.contactColor,
                       &contactColor, SHADER_UNIFORM_VEC4);

        DrawCoreSphere(ShieldCentre(shield->pos, radius), radius, rings, rings, WHITE);
    }
}

/* Compatibility entry point. The shell no longer samples the framebuffer,
 * but keeps both optical interfaces for a volumetric glass look. */
void VFX_ShieldShell_DrawRefraction(Camera3D cam)
{
    ShieldShell_InitShared();

    // Immediate-mode sphere normals are view-space. Convert the environment
    // key light once per draw so the glass highlight follows the actual camera
    // orientation instead of dotting a world-space light with view-space N.
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 camUp = Vector3CrossProduct(right, forward);
    s_shieldCameraPosition = cam.position;
    s_shieldCameraRight = right;
    s_shieldCameraUp = camUp;
    s_shieldCameraForward = forward;
    Vector3 lightWorld = Vector3Negate(Environment_GetSunDirection());
    Vector3 lightView = {
        Vector3DotProduct(lightWorld, right),
        Vector3DotProduct(lightWorld, camUp),
        -Vector3DotProduct(lightWorld, forward)
    };
    lightView = Vector3Normalize(lightView);

    VFXResolvedAppearance shieldAppearance;
    VFXResolvedAppearance shieldLegacy = {
        VFX_SURFACE_ALPHA, VFX_CONTRAST_MAGIC, 1.0f, 3.0f, 0.78f, true
    };
    VFXRenderScope body = VFXRender_BeginAppearance(
        VFX_RENDER_PASS_BODY, VFX_APPEARANCE_MAGIC, shieldLegacy, false,
        &shieldAppearance);
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_shieldShader.shader);
    ShieldShell_BindInputs(cam);
    if (s_shieldShader.lightDirView >= 0)
        SetShaderValue(s_shieldShader.shader, s_shieldShader.lightDirView,
                       &lightView, SHADER_UNIFORM_VEC3);
    /* This is a closed transparent volume, so both windings must reach BOTH
     * optical passes.  Culling a wall by render state is fragile: a later
     * pass can inherit the previous cull face and leave the far wall with
     * coverage but no radiance.  The fragment shader classifies the face via
     * gl_FrontFacing instead, which makes the two interfaces inseparable from
     * the one submission that draws them. */
    /* FAR WALL FIRST, THEN NEAR — two culled submissions, not one unculled one.
     *
     * This used to be a single rlDisableBackfaceCulling() draw, with the shader
     * classifying each fragment by gl_FrontFacing. That gives both walls into one
     * submission, but it gives them NO ORDER: a closed transparent volume has two
     * surfaces over almost every pixel, and with depth-write off the winner is
     * whichever the mesh happened to emit last. Measured by colouring
     * gl_FrontFacing and sign(normal.y) into two channels, the sphere came out in
     * three bands — far-upper, near-upper, near-lower — and the FAR-LOWER wall
     * never appeared at all. The band boundary is where the emission order flips,
     * which on a sphere projects to an ellipse: the hard seam across the middle.
     *
     * Culling per pass restores the order. The old comment here warned that
     * "culling a wall by render state is fragile: a later pass can inherit the
     * previous cull face" — true, and the answer is to SET the face explicitly on
     * every pass rather than to avoid culling. Both passes still flush around the
     * state change: ENGINE_LANDMINES says the batch-flush rule covers cull face,
     * not just depth. */
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);   /* keep back faces: the FAR wall */
    rlDrawRenderBatchActive();
    ShieldShell_DrawPass(false);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);    /* keep front faces: the NEAR wall */
    rlDrawRenderBatchActive();
    ShieldShell_DrawPass(false);
    rlDrawRenderBatchActive();
    SkillManager_EndShader();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    VFXRender_EndDraw(&body);

    VFXRenderScope emission = VFXRender_BeginAppearance(
        VFX_RENDER_PASS_EMISSION, VFX_APPEARANCE_MAGIC, shieldLegacy, false,
        &shieldAppearance);
    rlDrawRenderBatchActive();
    SkillManager_BeginShader(s_shieldShader.shader);
    ShieldShell_BindInputs(cam);
    if (s_shieldShader.lightDirView >= 0)
        SetShaderValue(s_shieldShader.shader, s_shieldShader.lightDirView,
                       &lightView, SHADER_UNIFORM_VEC3);
    /* Mirror the body submission: the far interface owns half of the ground
     * contact line, so it must emit in the same draw that gives it coverage. */
    /* FAR WALL FIRST, THEN NEAR — two culled submissions, not one unculled one.
     *
     * This used to be a single rlDisableBackfaceCulling() draw, with the shader
     * classifying each fragment by gl_FrontFacing. That gives both walls into one
     * submission, but it gives them NO ORDER: a closed transparent volume has two
     * surfaces over almost every pixel, and with depth-write off the winner is
     * whichever the mesh happened to emit last. Measured by colouring
     * gl_FrontFacing and sign(normal.y) into two channels, the sphere came out in
     * three bands — far-upper, near-upper, near-lower — and the FAR-LOWER wall
     * never appeared at all. The band boundary is where the emission order flips,
     * which on a sphere projects to an ellipse: the hard seam across the middle.
     *
     * Culling per pass restores the order. The old comment here warned that
     * "culling a wall by render state is fragile: a later pass can inherit the
     * previous cull face" — true, and the answer is to SET the face explicitly on
     * every pass rather than to avoid culling. Both passes still flush around the
     * state change: ENGINE_LANDMINES says the batch-flush rule covers cull face,
     * not just depth. */
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);   /* keep back faces: the FAR wall */
    rlDrawRenderBatchActive();
    ShieldShell_DrawPass(true);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);    /* keep front faces: the NEAR wall */
    rlDrawRenderBatchActive();
    ShieldShell_DrawPass(true);
    rlDrawRenderBatchActive();
    SkillManager_EndShader();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    VFXRender_EndDraw(&emission);
}

/* Archetype pair kept for sync_vfx_test.py — the generated dispatch inside
 * VFX_Compose_Draw3D calls this during the 3D pass. The shield must NOT draw
 * here: its refraction samples a scene snapshot that does not exist until the
 * whole 3D pass completed, so the real draw is VFX_ShieldShell_DrawRefraction
 * in main.c's post-3D post-pass. */
static void VC_ShieldShell_Draw3D(Camera3D cam)
{
    // The shell itself draws after the 3D pass (see the note above), but its DEPTH
    // REGION REQUEST has to happen INSIDE it. SceneTargets_Begin() clears
    // s_softDepthRegionValid, so that flag's lifetime is exactly the 3D pass:
    //
    //     Begin -> valid=false ... 3D pass ... End -> SnapshotDepth reads the flag
    //
    // A request made from the post-3D draw lands after SnapshotDepth has already run and
    // is wiped by the next Begin, so the shell could never arm the snapshot from where it
    // draws — `depthContact()` read zeros forever and toggling shield_shell_depth_enabled
    // changed 0.000% of pixels. This stub already runs in the right place and already has
    // the camera, which is why it is worth having rather than deleting.
    if (s_shieldDepthEnabled <= 0.5f) return;
    const float screenH = (float)GetScreenHeight();
    const float halfFovy = cam.fovy * 0.5f * DEG2RAD;
    bool hasBounds = false;
    Rectangle bounds = {0};
    for (int i = 0; i < VFX_SHIELD_SHELL_MAX; ++i)
    {
        const VC_ShieldShell *sh = &s_shieldShells[i];
        if (!sh->active) continue;
        Vector3 centrePos = ShieldCentre(sh->pos, sh->radius);
        float distance = Vector3Length(Vector3Subtract(centrePos, cam.position));
        if (distance <= 0.001f) continue;
        Vector2 centre = GetWorldToScreen(centrePos, cam);
        float radiusPx = sh->radius * screenH / (2.0f * distance * tanf(halfFovy));
        Rectangle r = {centre.x - radiusPx, centre.y - radiusPx, radiusPx * 2.0f, radiusPx * 2.0f};
        if (!hasBounds) { bounds = r; hasBounds = true; }
        else {
            float x1 = fmaxf(bounds.x + bounds.width, r.x + r.width);
            float y1 = fmaxf(bounds.y + bounds.height, r.y + r.height);
            bounds.x = fminf(bounds.x, r.x); bounds.y = fminf(bounds.y, r.y);
            bounds.width = x1 - bounds.x; bounds.height = y1 - bounds.y;
        }
    }
    if (hasBounds) SceneTargets_RequestSoftDepthRegion(bounds);
}
