// Unifed Path/Beam/Chain Link visual composition.
// Unifies straight beams, energy flows, and sagging chain links into a single
// path-drawing logic that transforms points over time and handles extending progress.

void VFX_ComposePathLink(VC_MaterialId matId, const Vector3 *points, int count, float width, float progress, float time)
{
    if (count < 2 || progress <= 0.0f)
        return;

    // 1. Fade logic to prevent popping
    float fadeAlpha = (progress > 0.85f) ? fmaxf(0.0f, (1.0f - progress) / 0.15f) : 1.0f;
    if (fadeAlpha <= 0.0f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    Color bodyCol = (mat->blendMode == BLEND_ALPHA) ? VC_WithAlpha(mat->body, (unsigned char)(200 * fadeAlpha))
                                                    : VC_WithAlpha(mat->body, (unsigned char)(255 * fadeAlpha));
    Color glowCol = VC_WithAlpha(mat->glow, (unsigned char)(255 * fadeAlpha));

    // Calculate total path length
    float pathLen = 0.0f;
    for (int i = 0; i < count - 1; i++)
    {
        pathLen += Vector3Distance(points[i], points[i + 1]);
    }
    if (pathLen < 0.01f)
        return;

    // Define direction from start to end (for local coordinate frame wiggling)
    Vector3 start = points[0];
    Vector3 end = points[count - 1];
    Vector3 chainDir = Vector3Subtract(end, start);
    float chainLen = Vector3Length(chainDir);
    Vector3 dir = (chainLen > 0.001f) ? Vector3Scale(chainDir, 1.0f / chainLen) : (Vector3){0, 0, 1};

    // Construct perpendicular frame
    Vector3 upProj = {0.0f, 1.0f, 0.0f};
    if (fabsf(Vector3DotProduct(dir, upProj)) > 0.95f) {
        upProj = (Vector3){1.0f, 0.0f, 0.0f};
    }
    Vector3 rightVec = Vector3Normalize(Vector3CrossProduct(dir, upProj));
    Vector3 upVec = Vector3Normalize(Vector3CrossProduct(rightVec, dir));

    // Sample path segments up to progress
    #define CHAIN_SEGMENTS 16
    Vector3 pts[CHAIN_SEGMENTS];

    // Helper to sample path at normalized parameter u (0..1)
    for (int i = 0; i < CHAIN_SEGMENTS; i++)
    {
        float t = (float)i / (float)(CHAIN_SEGMENTS - 1);
        float progress_capped = fminf(progress / 0.85f, 1.0f);
        float u = t * progress_capped;

        // Sample base path
        float floatIdx = u * (count - 1);
        int idx = (int)floatIdx;
        float f = floatIdx - idx;
        Vector3 basePos;
        if (idx >= count - 1)
        {
            basePos = points[count - 1];
        }
        else
        {
            basePos = Vector3Lerp(points[idx], points[idx + 1], f);
        }

        // Apply dynamic wave wiggling
        float env = sinf(t * 3.14159265f); // envelope: zero at start and end of ACTIVE segment

        // Wave amplitude depends on material (steel chain sways much less than fire/lightning)
        float waveAmp1 = 0.0f;
        float waveAmp2 = 0.0f;
        if (matId == VC_MAT_WOOD || matId == VC_MAT_POISON || matId == VC_MAT_WATER ||
            matId == VC_MAT_FIRE || matId == VC_MAT_LIGHTNING || matId == VC_MAT_VOID ||
            matId == VC_MAT_ICE || matId == VC_MAT_QI || matId == VC_MAT_HOLY)
        {
            waveAmp1 = pathLen * 0.12f;
            waveAmp2 = pathLen * 0.08f;
        }
        else
        {
            // Physical metal/earth chain: very subtle organic weight sway
            waveAmp1 = pathLen * 0.02f;
            waveAmp2 = pathLen * 0.01f;
        }

        float waveFreq1 = 6.0f;
        float waveSpeed1 = 8.0f;
        float waveFreq2 = 4.0f;
        float waveSpeed2 = -5.0f;

        pts[i] = Vector3Add(basePos, Vector3Scale(rightVec, sinf(t * waveFreq1 + time * waveSpeed1) * waveAmp1 * env));
        pts[i] = Vector3Add(pts[i], Vector3Scale(upVec, cosf(t * waveFreq2 + time * waveSpeed2) * waveAmp2 * env));

        // Clamp Y to prevent ground clipping
        pts[i].y = fmaxf(pts[i].y, 0.05f);
    }

    // 2. Render Style Selection
    bool isPhysical = (matId == VC_MAT_METAL || matId == VC_MAT_EARTH || matId == VC_MAT_TAIJI);
    bool isOrganic = (matId == VC_MAT_WOOD || matId == VC_MAT_POISON || matId == VC_MAT_WATER);

    if (isPhysical)
    {
        // Style A: Physical Torus Chain
        rlDrawRenderBatchActive();
        BeginBlendMode(mat->blendMode);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        EffectMaterialParams ep = {0};
        ep.baseColor = bodyCol;
        ep.rimStrength = 2.0f;
        ep.fresnelPower = 2.5f;
        ep.emissiveIntensity = (matId == VC_MAT_METAL) ? 0.9f : 1.3f;
        ep.translucency = 0.1f;

        EffectMaterial torusMat = Material_LoadCustom(ep);
        Material_Begin(torusMat);

        for (int i = 0; i < CHAIN_SEGMENTS - 1; i++)
        {
            Vector3 p0 = pts[i];
            Vector3 p1 = pts[i + 1];

            Vector3 segDir = Vector3Subtract(p1, p0);
            float segLen = Vector3Length(segDir);
            if (segLen < 0.01f)
                continue;
            Vector3 center = Vector3Lerp(p0, p1, 0.5f);

            Vector3 tangent = Vector3Scale(segDir, 1.0f / segLen);
            Vector3 defaultUp = {0, 1, 0};
            float dot = Vector3DotProduct(tangent, defaultUp);
            Vector3 rotAxis;
            float angle = 0.0f;
            if (fabsf(dot) > 0.999f)
            {
                rotAxis = (Vector3){1, 0, 0};
                angle = (dot > 0.0f) ? 0.0f : 180.0f;
            }
            else
            {
                rotAxis = Vector3Normalize(Vector3CrossProduct(defaultUp, tangent));
                angle = acosf(dot) * 57.29578f;
            }

            rlPushMatrix();
            rlTranslatef(center.x, center.y, center.z);
            rlRotatef(angle, rotAxis.x, rotAxis.y, rotAxis.z);
            rlRotatef(i * 90.0f + time * 60.0f, 0, 1, 0);

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
        // Style B: Organic Spline (Vine/Water)
        static RibbonPoint ribbonPoints[CHAIN_SEGMENTS];
        for (int i = 0; i < CHAIN_SEGMENTS; i++)
        {
            float norm = (float)i / (float)(CHAIN_SEGMENTS - 1);
            float taper = powf(1.0f - norm, 0.35f);
            if (taper < 0.15f)
                taper = 0.15f;

            ribbonPoints[i].position = pts[i];
            ribbonPoints[i].halfWidth = width * 0.75f * taper;
            ribbonPoints[i].v = norm - time * 0.8f;
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

        // Pass 2: inner hot core (white additive thread)
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
        // Style C: Energy / Plasma beam
        Texture2D energyTex = ResourceManager_LoadTexture("assets/textures/energy_flow.png");
        float tiling = pathLen / 3.5f;

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

    // Spawns glowing particles at the shooting front tip
    float dt = GetFrameTime();
    if (progress < 0.95f && Random01() < (40.0f * dt))
    {
        Vector3 tipPos = pts[CHAIN_SEGMENTS - 1];
        SpawnParticle((ParticleConfig){
            .position = tipPos,
            .velocity = {
                (Random01() - 0.5f) * 1.6f,
                (Random01() - 0.5f) * 1.6f + 1.2f,
                (Random01() - 0.5f) * 1.6f
            },
            .colorStart = glowCol,
            .colorEnd = ColorAlpha(mat->body, 0.0f),
            .radius = width * 0.3f * (0.8f + Random01() * 0.5f),
            .lifetime = 0.4f + Random01() * 0.3f
        });
    }
}

void VFX_ComposeChainLink(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float sag, float progress, float time)
{
    // 1. Generate base sagging Bezier path
    #define BASE_PATH_SEGS 16
    Vector3 pts[BASE_PATH_SEGS];
    Vector3 mid = Vector3Lerp(start, end, 0.5f);
    mid.y -= sag;

    for (int i = 0; i < BASE_PATH_SEGS; i++)
    {
        float t = (float)i / (float)(BASE_PATH_SEGS - 1);
        float omt = 1.0f - t;
        pts[i] = Vector3Add(
            Vector3Add(Vector3Scale(start, omt * omt), Vector3Scale(mid, 2.0f * omt * t)),
            Vector3Scale(end, t * t)
        );
    }

    // 2. Call unified path link drawer
    VFX_ComposePathLink(matId, pts, BASE_PATH_SEGS, width, progress, time);
}
