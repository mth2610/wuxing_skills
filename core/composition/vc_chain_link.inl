// Drooping Spline Chain / Vine Link visual composition.
// progress (0..1) shoots the chain from start to end.
// sag controls how much the curve sags under gravity.
// time drives texture scrolling, waviness, and rotation.

typedef struct {
    Vector3 position;
    Vector3 normal;
    float size;
    float elapsed;
    float lifetime;
    Color color;
    bool active;
} ChainSmokePuff;

#define MAX_CHAIN_SMOKE_PUFFS 32
static ChainSmokePuff s_chainSmokes[MAX_CHAIN_SMOKE_PUFFS];
static bool s_chainSmokesInit = false;

void VFX_ComposeChainLink(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float sag, float progress, float time)
{
    if (progress <= 0.0f)
        return;

    // 1. Fade logic to prevent popping
    float fadeAlpha = (progress > 0.85f) ? fmaxf(0.0f, (1.0f - progress) / 0.15f) : 1.0f;
    if (fadeAlpha <= 0.0f)
        return;

    // Initialize local smoke pool if not done
    if (!s_chainSmokesInit)
    {
        for (int i = 0; i < MAX_CHAIN_SMOKE_PUFFS; i++)
            s_chainSmokes[i].active = false;
        s_chainSmokesInit = true;
    }

    // Update smoke elapsed time once per frame
    float dt = GetFrameTime();
    static float s_lastUpdateTime = -1.0f;
    if (time != s_lastUpdateTime)
    {
        for (int i = 0; i < MAX_CHAIN_SMOKE_PUFFS; i++)
        {
            if (s_chainSmokes[i].active)
            {
                s_chainSmokes[i].elapsed += dt;
                if (s_chainSmokes[i].elapsed >= s_chainSmokes[i].lifetime)
                {
                    s_chainSmokes[i].active = false;
                }
            }
        }
        s_lastUpdateTime = time;
    }

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color bodyCol = (mat->blendMode == BLEND_ALPHA) ? VC_WithAlpha(mat->body, (unsigned char)(200 * fadeAlpha))
                                                    : VC_WithAlpha(mat->body, (unsigned char)(255 * fadeAlpha));
    Color glowCol = VC_WithAlpha(mat->glow, (unsigned char)(255 * fadeAlpha));

    // 2. Bezier Curve Sampling
    // Sample points along a quadratic Bezier curve with sag in Y axis
    #define CHAIN_SEGMENTS 16
    Vector3 pts[CHAIN_SEGMENTS];
    Vector3 mid = Vector3Lerp(start, end, 0.5f);
    mid.y -= sag; // droop under gravity

    for (int i = 0; i < CHAIN_SEGMENTS; i++)
    {
        float t = (float)i / (float)(CHAIN_SEGMENTS - 1);
        // Extend the curve according to progress (shoots from start to end)
        float t_curr = t * fminf(progress / 0.85f, 1.0f);
        float omt = 1.0f - t_curr;
        pts[i] = Vector3Add(
            Vector3Add(Vector3Scale(start, omt * omt), Vector3Scale(mid, 2.0f * omt * t_curr)),
            Vector3Scale(end, t_curr * t_curr)
        );
        // Prevent clipping below ground (Y = 0) with a minor offset to prevent Z-fighting
        pts[i].y = fmaxf(pts[i].y, 0.05f);
    }

    // 3. Render Style Selection
    bool isPhysical = (matId == VC_MAT_METAL || matId == VC_MAT_EARTH || matId == VC_MAT_TAIJI);
    bool isOrganic = (matId == VC_MAT_WOOD || matId == VC_MAT_POISON || matId == VC_MAT_WATER);

    if (isPhysical)
    {
        // ─────────────────────────────────────────────────────────────────────
        // STYLE A: Physical Interlocking 3D Chain (torus loops)
        // ─────────────────────────────────────────────────────────────────────
        rlDrawRenderBatchActive();
        BeginBlendMode(mat->blendMode);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        EffectMaterialParams ep = {0};
        ep.baseColor = bodyCol;
        ep.rimStrength = 2.0f;
        ep.fresnelPower = 2.5f;
        ep.emissiveIntensity = (matId == VC_MAT_METAL) ? 0.9f : 1.3f;
        ep.translucency = (matId == VC_MAT_METAL) ? 0.15f : 0.5f;

        EffectMaterial eMat = Material_LoadCustom(ep);
        Material_Begin(eMat);

        // Draw individual interlocking rings along active segments
        int activeCount = (int)(progress * (CHAIN_SEGMENTS - 1));
        if (activeCount < 1)
            activeCount = 1;
        if (activeCount > CHAIN_SEGMENTS - 1)
            activeCount = CHAIN_SEGMENTS - 1;

        for (int i = 0; i < activeCount; i++)
        {
            Vector3 p0 = pts[i];
            Vector3 p1 = pts[i + 1];
            Vector3 linkPos = Vector3Lerp(p0, p1, 0.5f);
            Vector3 linkDir = Vector3Normalize(Vector3Subtract(p1, p0));

            rlPushMatrix();
            rlTranslatef(linkPos.x, linkPos.y, linkPos.z);

            // Align local Y axis with the link direction
            Vector3 localUp = {0.0f, 1.0f, 0.0f};
            float dot = Vector3DotProduct(localUp, linkDir);
            if (fabsf(dot) < 0.999f)
            {
                Vector3 axis = Vector3Normalize(Vector3CrossProduct(localUp, linkDir));
                float rotAngle = acosf(dot) * RAD2DEG;
                rlRotatef(rotAngle, axis.x, axis.y, axis.z);
            }
            else if (dot < -0.999f)
            {
                rlRotatef(180.0f, 1.0f, 0.0f, 0.0f);
            }

            // Alternating 90-degree twist around link axis to interlock
            float rollAngle = (float)(i % 2) * 90.0f;
            rlRotatef(rollAngle, 0.0f, 1.0f, 0.0f);

            // Link dimensions based on segment width
            float outerR = width * 1.1f;
            float innerR = width * 0.45f;
            DrawCoreTorus((Vector3){0, 0, 0}, innerR, outerR, 4, 8, WHITE);

            rlPopMatrix();
        }

        Material_End();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
        rlDrawRenderBatchActive();
    }
    else if (isOrganic)
    {
        // ─────────────────────────────────────────────────────────────────────
        // STYLE B: Glowing Organic Vine / Fluid Spline (2-pass glowing ribbon)
        // ─────────────────────────────────────────────────────────────────────
        static RibbonPoint ribbonPoints[CHAIN_SEGMENTS];
        for (int i = 0; i < CHAIN_SEGMENTS; i++)
        {
            float norm = (float)i / (float)(CHAIN_SEGMENTS - 1);
            float taper = powf(1.0f - norm, 0.35f);
            if (taper < 0.15f)
                taper = 0.15f;

            ribbonPoints[i].position = pts[i];
            ribbonPoints[i].halfWidth = width * 0.75f * taper;
            ribbonPoints[i].v = norm - time * 0.8f; // scroll texture coordinate
            ribbonPoints[i].tint = bodyCol;
        }

        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ALPHA);

        // Pass 1: outer shell
        EffectMaterialParams matParams = {0};
        matParams.baseColor = ColorAlpha(mat->body, 0.8f * fadeAlpha);
        matParams.rimStrength = 2.2f;
        matParams.fresnelPower = 2.5f;
        matParams.emissiveIntensity = 1.6f;
        matParams.translucency = 0.45f;

        EffectMaterial matG = Material_LoadCustom(matParams);
        Material_Begin(matG);
        DrawRibbonStrip(ribbonPoints, CHAIN_SEGMENTS, (Texture2D){0}, camera);
        Material_End();

        // Pass 2: inner hot core (white additive)
        BeginBlendMode(BLEND_ADDITIVE);
        matParams.emissiveIntensity = 3.0f;
        matParams.translucency = 0.0f;
        EffectMaterial matGlow = Material_LoadCustom(matParams);

        Material_Begin(matGlow);
        for (int i = 0; i < CHAIN_SEGMENTS; i++)
        {
            ribbonPoints[i].halfWidth = width * 0.08f;
            ribbonPoints[i].tint = ColorAlpha(WHITE, fadeAlpha * 0.9f);
        }
        DrawRibbonStrip(ribbonPoints, CHAIN_SEGMENTS, (Texture2D){0}, camera);
        Material_End();

        EndBlendMode();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        rlDrawRenderBatchActive();
    }
    else
    {
        // ─────────────────────────────────────────────────────────────────────
        // STYLE C: Energy Conduit / Scrolling Plasma Ray
        // ─────────────────────────────────────────────────────────────────────
        Texture2D energyTex = ResourceManager_LoadTexture("assets/textures/energy_flow.png");

        float chainLen = Vector3Distance(start, end);
        float tiling = chainLen / 3.5f;

        RibbonEnergyFieldLayer layers[2] = {
            { .widthRatio = 0.8f, .breatheFreq = 16.0f, .breatheAmp = 0.07f,
              .scrollSpeed = -1.5f, .uvTiling = tiling, .vFlip = false,
              .useTexture = true, .color = bodyCol },
            { .widthRatio = 0.08f, .breatheFreq = 0.0f, .breatheAmp = 0.0f,
              .scrollSpeed = 0.0f, .uvTiling = 1.0f, .vFlip = false,
              .useTexture = false, .color = WHITE }
        };

        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        BeginBlendMode(mat->blendMode);

        DrawRibbonEnergyField(pts, CHAIN_SEGMENTS, width * 1.4f, NULL, layers, 2, energyTex,
                              RIBBON_WORLD_UP, (Vector3){0, 1, 0}, camera, time);

        EndBlendMode();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // particles: Spawns glowing particles at the shooting front tip
    // ─────────────────────────────────────────────────────────────────────────
    if (progress < 0.95f && Random01() < (40.0f * dt))
    {
        Vector3 tipPos = pts[CHAIN_SEGMENTS - 1];
        SpawnParticle((ParticleConfig){
            .position = tipPos,
            .velocity = {
                (Random01() - 0.5f) * 1.6f,
                (Random01() - 0.5f) * 1.6f + 1.2f, // upward spray
                (Random01() - 0.5f) * 1.6f
            },
            .colorStart = glowCol,
            .colorEnd = ColorAlpha(mat->body, 0.0f),
            .radius = width * 0.3f * (0.8f + Random01() * 0.5f),
            .lifetime = 0.4f + Random01() * 0.3f
        });
    }

    // Spawn new dust/smoke puffs along the chain as it moves/drags
    if (Random01() < (3.5f * dt))
    {
        int activeCount = (int)(progress * (CHAIN_SEGMENTS - 1));
        if (activeCount > 1)
        {
            int idx = GetRandomValue(0, activeCount - 1);
            Vector3 p0 = pts[idx];
            Vector3 p1 = pts[idx + 1];
            Vector3 spawnPos = Vector3Lerp(p0, p1, Random01());
            spawnPos.y = fmaxf(spawnPos.y, 0.05f); // align to ground

            for (int k = 0; k < MAX_CHAIN_SMOKE_PUFFS; k++)
            {
                if (!s_chainSmokes[k].active)
                {
                    s_chainSmokes[k] = (ChainSmokePuff){
                        .position = spawnPos,
                        .normal = (Vector3){0.0f, 1.0f, 0.0f}, // horizontal oriented quad
                        .size = width * (2.2f + Random01() * 2.2f),
                        .elapsed = 0.0f,
                        .lifetime = 0.7f + Random01() * 0.4f,
                        .color = bodyCol,
                        .active = true
                    };
                    break;
                }
            }
        }
    }

    // Draw active smoke puffs
    for (int k = 0; k < MAX_CHAIN_SMOKE_PUFFS; k++)
    {
        if (s_chainSmokes[k].active)
        {
            float prog = s_chainSmokes[k].elapsed / s_chainSmokes[k].lifetime;
            VFX_ComposeSmokeOnPlane(s_chainSmokes[k].position, s_chainSmokes[k].normal, s_chainSmokes[k].size, prog, s_chainSmokes[k].color);
        }
    }
}
