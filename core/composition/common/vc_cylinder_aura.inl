#include "rlgl.h"
#include "core/geometry/procedural_mesh_utils.h"

void VFX_ComposeCylinderAura(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time)
{
    if (progress <= 0.0f || progress >= 1.0f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    float dt = GetFrameTime();

    // Scale-in over first 20% of lifetime.
    float scaleIn = (progress < 0.2f) ? (progress / 0.2f) : 1.0f;
    float currentRadius = radius * scaleIn;

    float breathe = 1.0f + 0.03f * sinf(time * 3.0f);
    float r = currentRadius * breathe;
    float auraHeight = currentRadius * 2.5f;
    float topY = pos.y + auraHeight;

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 1: AURA SHELL — element-tinted cylinder with custom shader
    // ─────────────────────────────────────────────────────────────────────────
    static AuraShellMaterial s_auraMat;
    static bool s_loaded = false;
    if (!s_loaded)
    {
        s_auraMat = AuraShellMaterial_Load((AuraShellMaterialParams){
            .bodyColor = WHITE, // overwritten below each frame
            .glowColor = WHITE,
            .opacity = 0.35f,
            .fresnelPower = 2.0f,
            .rimStrength = 0.6f,
            .scrollSpeed = 1.2f,
            .noiseScale = 4.0f,
            .heightScale = 1.8f,
            .scanFreq = 9.0f,
            .scanSpeed = 1.4f,
            .scanStrength = 0.35f,
            .displaceAmp = 0.0f,
            .topY = 0.0f, // guard: 0 = skip fade (uploaded each frame)
        });
        s_loaded = true;
    }

    // Element colors + geometry bounds update every frame.
    s_auraMat.params.bodyColor = VC_WithAlpha(mat->body, 230);
    s_auraMat.params.glowColor = VC_WithAlpha(mat->glow, 255);
    s_auraMat.params.displaceAmp = r * 0.04f;
    s_auraMat.params.topY = topY;

    VortexFunnelConfig fCfg = ProceduralMesh_DefaultVortexFunnelConfig();
    fCfg.bottomRadius = r * 1.1f;
    fCfg.topRadius = r * 1.1f;
    fCfg.height = auraHeight;
    fCfg.twistAmount = 0.0f;
    fCfg.ridgeAmount = 0.0f;

    VortexFunnelMeshData funnelData;
    ProceduralMesh_BuildVortexFunnel(&funnelData, pos, &fCfg, 14, 28, 0.0f);

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    AuraShellMaterial_Begin(s_auraMat);
    ProceduralMesh_DrawVortexFunnel(&funnelData, WHITE);
    AuraShellMaterial_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 2: GROUND RUNE — rotating elemental sigil at the base
    // ─────────────────────────────────────────────────────────────────────────
    {
        const char *runePath = mat->runeDecal;
        if (runePath)
        {
            Texture2D runeTex = ResourceManager_LoadTexture(runePath);
            float runeR = r * 0.9f * scaleIn;
            unsigned char runeA = (unsigned char)(60 + 80 * scaleIn);
            rlDrawRenderBatchActive();
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthMask();
            // Outer wide slow ring + inner fast ring for layered depth.
            VC_DrawGroundRune(runeTex,
                              (Vector3){pos.x, pos.y + 0.015f, pos.z},
                              runeR, time * 18.0f,
                              VC_WithAlpha(mat->glow, runeA));
            VC_DrawGroundRune(runeTex,
                              (Vector3){pos.x, pos.y + 0.02f, pos.z},
                              runeR * 0.55f, -(time * 32.0f),
                              VC_WithAlpha(mat->body, (unsigned char)(runeA * 0.7f)));
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 3: CORE RISING EMBERS — bright particles climbing the inner column
    // ─────────────────────────────────────────────────────────────────────────
    if (Random01() < (8.0f * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float innerR = r * 0.88f * sqrtf(Random01()); // sqrtf = uniform disc
        Vector3 spawnP = {
            pos.x + cosf(theta) * innerR,
            pos.y + 0.05f,
            pos.z + sinf(theta) * innerR};
        float speed = 1.2f + Random01() * 1.6f;
        SpawnParticle((ParticleConfig){
            .position = spawnP,
            .velocity = {(Random01() - 0.5f) * 0.10f, speed, (Random01() - 0.5f) * 0.10f},
            .colorStart = VC_WithAlpha(mat->glow, 200),
            .colorEnd = VC_WithAlpha(mat->glow, 0),
            .radius = (0.016f + Random01() * 0.018f) * currentRadius,
            .lifetime = auraHeight / speed * (0.65f + Random01() * 0.35f),
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 4: CURL WISPS spiraling around the outer column wall
    // ─────────────────────────────────────────────────────────────────────────
    static ForceField s_auraCurlFld = {0};
    static bool s_wispInit = false;
    if (!s_wispInit)
    {
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_NOISE_CURL, .strength = 4.5f, .noiseScale = 10.0f, .noiseSpeed = 1.5f});
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_VISCOSITY, .strength = 10.0f});
        ForceField_AddLayer(&s_auraCurlFld, (ForceLayer){
                                                .type = FORCE_GRAVITY_DIR,
                                                .direction = (Vector3){0, 1.0f, 0},
                                                .strength = 3.0f});
        s_wispInit = true;
    }

    if (Random01() < (40.0f * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float h = Random01() * auraHeight;

        Vector3 spawnPos = {
            pos.x + cosf(theta) * (r * 1.1f),
            pos.y + h,
            pos.z + sinf(theta) * (r * 1.1f)};
        Vector3 outDir = {cosf(theta), 0.0f, sinf(theta)};

        TrailConfig tCfg = {0};
        tCfg.type = TRAIL_TYPE_WISP;
        tCfg.pos = spawnPos;
        tCfg.vel = Vector3Scale(outDir, currentRadius * 0.15f);
        tCfg.tint = WHITE;
        tCfg.blendMode = BLEND_ADDITIVE;
        tCfg.life = 0.35f + Random01() * 0.45f;
        tCfg.thick = 0.012f * currentRadius;
        tCfg.len = currentRadius * 0.35f;
        tCfg.gradient = mat->hotGrad;
        tCfg.forceField = &s_auraCurlFld;
        tCfg.priority = 0;

        SpawnTrailEntity(tCfg);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LAYER 5: AMBIENT LIGHT pulse (mid-height)
    // ─────────────────────────────────────────────────────────────────────────
    if (GetRandomValue(0, 100) < 10)
    {
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, auraHeight * 0.4f, 0}),
                       mat->glow, r * 2.8f, 0.18f, VFX_PRIORITY_LOW);
    }
}
