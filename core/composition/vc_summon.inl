static void DrawGroundNeonLine(Vector3 p1, Vector3 p2, float thickness, Color col)
{
    Vector3 dir = Vector3Subtract(p2, p1);
    float dist = Vector3Length(dir);
    if (dist < 0.0001f)
        return;
    dir = Vector3Scale(dir, 1.0f / dist);

    Vector3 right = {-dir.z, 0.0f, dir.x};
    float halfT = thickness * 0.5f;

    Vector3 v1 = Vector3Add(p1, Vector3Scale(right, -halfT));
    Vector3 v2 = Vector3Add(p2, Vector3Scale(right, -halfT));
    Vector3 v3 = Vector3Add(p2, Vector3Scale(right, halfT));
    Vector3 v4 = Vector3Add(p1, Vector3Scale(right, halfT));

    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(v1.x, v1.y, v1.z);
    rlVertex3f(v2.x, v2.y, v2.z);
    rlVertex3f(v3.x, v3.y, v3.z);
    rlVertex3f(v4.x, v4.y, v4.z);
}

static void DrawGroundNeonSquare(Vector3 center, float size, float angleRad, float thickness, Color col)
{
    float d = size * 0.5f;
    Vector3 pts[4] = {
        {-d, 0.0f, -d},
        {d, 0.0f, -d},
        {d, 0.0f, d},
        {-d, 0.0f, d}};

    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);

    for (int i = 0; i < 4; i++)
    {
        float rx = pts[i].x * cosA - pts[i].z * sinA;
        float rz = pts[i].x * sinA + pts[i].z * cosA;
        pts[i] = (Vector3){center.x + rx, center.y, center.z + rz};
    }

    rlBegin(RL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        DrawGroundNeonLine(pts[i], pts[(i + 1) % 4], thickness, col);
    }
    rlEnd();
}

static void DrawGroundNeonCircle(Vector3 center, float radius, float thickness, Color col, int segments)
{
    if (segments < 4)
        segments = 4;
    Vector3 pts[64];
    if (segments > 64)
        segments = 64;

    for (int i = 0; i < segments; i++)
    {
        float angle = (float)i / (float)segments * 2.0f * PI;
        pts[i] = (Vector3){
            center.x + cosf(angle) * radius,
            center.y,
            center.z + sinf(angle) * radius};
    }

    rlBegin(RL_QUADS);
    for (int i = 0; i < segments; i++)
    {
        DrawGroundNeonLine(pts[i], pts[(i + 1) % segments], thickness, col);
    }
    rlEnd();
}

void VFX_SummonCircle(Vector3 pos, float radius, float progress, float time, Color color)
{
    float alpha = 1.0f;
    if (progress > 0.8f)
    {
        alpha = (1.0f - progress) / 0.2f;
        if (alpha < 0.0f)
            alpha = 0.0f;
    }

    float currentRadius = radius * fminf(progress / 0.15f, 1.0f);
    unsigned char finalAlpha = (unsigned char)(230 * alpha);
    Color col = (Color){color.r, color.g, color.b, finalAlpha};

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    float yOffset = 0.01f;
    Vector3 drawPos = {pos.x, pos.y + yOffset, pos.z};

    // 1. Draw concentric circles
    DrawGroundNeonCircle(drawPos, currentRadius, 0.015f * currentRadius, col, 36);
    DrawGroundNeonCircle(drawPos, currentRadius * 0.7f, 0.01f * currentRadius, col, 24);

    // 2. Draw Outer Square (Clockwise)
    DrawGroundNeonSquare(drawPos, currentRadius * 1.4f, time * 1.5f, 0.015f * currentRadius, col);

    // 3. Draw Inner Diamond (Counter-Clockwise)
    DrawGroundNeonSquare(drawPos, currentRadius * 1.0f, time * -2.2f, 0.012f * currentRadius, col);

    // 4. Draw small squares at the corners of the outer rotating square
    float d = currentRadius * 1.4f * 0.5f;
    float cosA = cosf(time * 1.5f);
    float sinA = sinf(time * 1.5f);
    Vector3 corners[4] = {
        {-d, 0.0f, -d},
        {d, 0.0f, -d},
        {d, 0.0f, d},
        {-d, 0.0f, d}};
    for (int i = 0; i < 4; i++)
    {
        float rx = corners[i].x * cosA - corners[i].z * sinA;
        float rz = corners[i].x * sinA + corners[i].z * cosA;
        Vector3 cornerPos = (Vector3){drawPos.x + rx, drawPos.y + 0.001f, drawPos.z + rz};
        DrawGroundNeonSquare(cornerPos, currentRadius * 0.22f, time * -3.0f, 0.008f * currentRadius, col);
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // 5. Spawn pulling particles sucking in
    if (GetRandomValue(0, 100) < 20 && progress < 0.8f)
    {
        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        float dist = currentRadius * (1.0f + ((float)rand() / (float)RAND_MAX) * 0.4f);
        Vector3 spawnPos = {
            pos.x + cosf(angle) * dist,
            pos.y + 0.05f + ((float)rand() / (float)RAND_MAX) * 0.5f,
            pos.z + sinf(angle) * dist};
        Vector3 toCenter = Vector3Subtract(pos, spawnPos);
        Vector3 vel = Vector3Scale(Vector3Normalize(toCenter), 1.2f);

        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = vel,
            .radius = (0.015f + ((float)rand() / (float)RAND_MAX) * 0.025f) * radius,
            .lifetime = 0.5f,
            .colorStart = color,
            .colorEnd = (Color){color.r, color.g, color.b, 0}});
    }

    // 6. Center glow light
    VFXLight_Spawn(pos, color, currentRadius * 1.5f, 0.05f, VFX_PRIORITY_LOW);
}

void VFX_ComposeQiAura(VC_MaterialId matId, Vector3 casterPos, float progress, float time, float radius)
{
    // Khí công pastel theo nguyên tố; khí thuần (xianxia trắng-lam) → VC_MAT_QI.
    Color color = VFX_Material(matId)->soft;

    // 1. Calculate breathing phase & alpha
    float fadeAlpha = 1.0f;
    if (progress < 0.15f)
    {
        fadeAlpha = progress / 0.15f; // fade in
    }
    else if (progress > 0.8f)
    {
        fadeAlpha = (1.0f - progress) / 0.2f; // fade out
    }
    if (fadeAlpha < 0.0f)
        fadeAlpha = 0.0f;

    float opacity = 0.22f; // thin wispy Xianxia feel
    float collapse = (progress > 0.8f) ? (1.0f - (progress - 0.8f) / 0.2f) : 1.0f;

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    int ribbonCount = 5;
    float ribbonWidth = 0.018f;
    Color ribbonCol = (Color){color.r, color.g, color.b, (unsigned char)(255 * opacity * fadeAlpha)};

    for (int s = 0; s < ribbonCount; s++)
    {
        float baseAngle = (float)s / ribbonCount * 2.0f * PI + time * 0.8f;

        // Bezier control points to build sweeping organic orbits like the reference image
        Vector3 p0 = {
            casterPos.x + cosf(baseAngle) * radius * 1.2f * collapse,
            casterPos.y + 0.05f,
            casterPos.z + sinf(baseAngle) * radius * 1.2f * collapse};

        // Convergence point at caster's chest/hand
        Vector3 p3 = {
            casterPos.x + cosf(baseAngle + PI * 2.0f) * 0.12f * collapse,
            casterPos.y + 0.75f,
            casterPos.z + sinf(baseAngle + PI * 2.0f) * 0.12f * collapse};

        // Mid-low swept wide
        float a1 = baseAngle + PI * 0.7f;
        Vector3 p1 = {
            casterPos.x + cosf(a1) * radius * 1.6f * collapse,
            casterPos.y + 0.3f,
            casterPos.z + sinf(a1) * radius * 1.6f * collapse};

        // Mid-high closer
        float a2 = baseAngle + PI * 1.4f;
        Vector3 p2 = {
            casterPos.x + cosf(a2) * radius * 0.7f * collapse,
            casterPos.y + 0.6f,
            casterPos.z + sinf(a2) * radius * 0.7f * collapse};

        rlBegin(RL_QUADS);
        rlColor4ub(ribbonCol.r, ribbonCol.g, ribbonCol.b, ribbonCol.a);

        int segments = 24;
        for (int j = 0; j < segments; j++)
        {
            float t1 = (float)j / segments;
            float t2 = (float)(j + 1) / segments;

            // Grow along Bezier path based on casting progress
            float drawLimit = progress / 0.85f;
            if (drawLimit > 1.0f)
                drawLimit = 1.0f;
            t1 *= drawLimit;
            t2 *= drawLimit;

            Vector3 pt1 = ProceduralMesh_BezierPoint(p0, p1, p2, p3, t1);
            Vector3 pt2 = ProceduralMesh_BezierPoint(p0, p1, p2, p3, t2);

            // Add sine-wave turbulence to make the wisps look wavy and alive
            float wave1 = sinf(t1 * 12.0f - time * 6.0f) * 0.06f * collapse;
            float wave2 = sinf(t2 * 12.0f - time * 6.0f) * 0.06f * collapse;

            Vector3 waveDir1 = Vector3Normalize((Vector3){-pt1.z + casterPos.z, 0.0f, pt1.x - casterPos.x});
            Vector3 waveDir2 = Vector3Normalize((Vector3){-pt2.z + casterPos.z, 0.0f, pt2.x - casterPos.x});

            pt1 = Vector3Add(pt1, Vector3Scale(waveDir1, wave1));
            pt2 = Vector3Add(pt2, Vector3Scale(waveDir2, wave2));

            // Generate ribbon quads perpendicular to tangent
            Vector3 tangent = Vector3Subtract(pt2, pt1);
            Vector3 normal = {-tangent.z, 0.0f, tangent.x};
            float len = Vector3Length(normal);
            if (len > 0.0001f)
            {
                normal = Vector3Scale(normal, 1.0f / len);
            }

            Vector3 v1 = Vector3Add(pt1, Vector3Scale(normal, -ribbonWidth));
            Vector3 v2 = Vector3Add(pt1, Vector3Scale(normal, ribbonWidth));
            Vector3 v3 = Vector3Add(pt2, Vector3Scale(normal, ribbonWidth));
            Vector3 v4 = Vector3Add(pt2, Vector3Scale(normal, -ribbonWidth));

            rlVertex3f(v1.x, v1.y, v1.z);
            rlVertex3f(v2.x, v2.y, v2.z);
            rlVertex3f(v3.x, v3.y, v3.z);
            rlVertex3f(v4.x, v4.y, v4.z);
        }
        rlEnd();
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // 2. Spawn converging particles (5-10 total)
    if (GetRandomValue(0, 100) < 14 && progress < 0.8f)
    {
        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        float dist = radius * (0.8f + ((float)rand() / (float)RAND_MAX) * 0.4f);
        Vector3 spawnPos = {
            casterPos.x + cosf(angle) * dist,
            casterPos.y + 0.1f + ((float)rand() / (float)RAND_MAX) * 1.2f,
            casterPos.z + sinf(angle) * dist};
        Vector3 target = {casterPos.x, casterPos.y + 0.65f, casterPos.z};
        Vector3 toChest = Vector3Subtract(target, spawnPos);
        Vector3 vel = Vector3Scale(Vector3Normalize(toChest), 0.7f);

        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = vel,
            .radius = 0.02f + ((float)rand() / (float)RAND_MAX) * 0.03f,
            .lifetime = 0.6f,
            .colorStart = color,
            .colorEnd = (Color){color.r, color.g, color.b, 0}});
    }
}

#define MAX_ACTIVE_AURAS 16

typedef struct
{
    int casterAgentId;
    Vector3 staticPos;
    int trailIds[8];
    int wispCount;
    float time;
    float scale;
    float bodyHeight;
    Color color;
    bool active;
} ActiveQiAura;

static ActiveQiAura s_activeAuras[MAX_ACTIVE_AURAS];

static ForceField s_wispForceField;
static bool s_wispForceFieldInit = false;

static void InitWispForceField(void)
{
    if (s_wispForceFieldInit)
        return;
    ForceField_Clear(&s_wispForceField);

    // Layer 1: FORCE_NOISE_CURL (Curl noise for sweeping organic wisps!)
    ForceLayer curlLayer = {0};
    curlLayer.type = FORCE_NOISE_CURL;
    curlLayer.strength = 0.85f;
    curlLayer.noiseScale = 0.5f; // frequency thấp -> lượn chậm mượt
    curlLayer.noiseSpeed = 0.6f;
    ForceField_AddLayer(&s_wispForceField, curlLayer);

    // Layer 2: FORCE_VORTEX_AXIS (xoáy tổng thể quanh người)
    ForceLayer vortexLayer = {0};
    vortexLayer.type = FORCE_VORTEX_AXIS;
    vortexLayer.strength = 1.2f;
    vortexLayer.radius = 0.0f;
    ForceField_AddLayer(&s_wispForceField, vortexLayer);

    // Layer 3: FORCE_DRAG (giữ dải khí nằm gọn)
    ForceLayer dragLayer = {0};
    dragLayer.type = FORCE_DRAG;
    dragLayer.strength = 0.6f;
    ForceField_AddLayer(&s_wispForceField, dragLayer);

    s_wispForceFieldInit = true;
}

void VFX_AttachQiAura(int casterAgentId, Vector3 anchorPos, float bodyHeight, EffectPresetType element, float scale, int wispCount)
{
    InitWispForceField();

    if (wispCount < 1)
        wispCount = 4;
    if (wispCount > 8)
        wispCount = 8;

    // Find a free slot in s_activeAuras
    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_AURAS; i++)
    {
        if (!s_activeAuras[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1)
        return; // Pool full

    ActiveQiAura *aura = &s_activeAuras[slot];
    aura->casterAgentId = casterAgentId;
    aura->staticPos = anchorPos;
    aura->wispCount = wispCount;
    aura->scale = scale;
    aura->bodyHeight = bodyHeight;
    aura->time = 0.0f;
    aura->active = true;

    // Pastel aura theo nguyên tố — slot `soft` của material table.
    aura->color = VFX_Material(VFX_MaterialFromPreset(element))->soft;

    Texture2D wispTex = ResourceManager_LoadTexture("assets/textures/tex_smoke_puff.png");

    Vector3 basePos = anchorPos;
    if (casterAgentId >= 0)
    {
        SkillManager_GetAgentPos(casterAgentId, &basePos);
    }

    for (int s = 0; s < wispCount; s++)
    {
        TrailConfig cfg = {0};
        cfg.type = TRAIL_TYPE_FOLLOWER;
        cfg.pos = basePos;
        cfg.vel = (Vector3){0, 0, 0};
        cfg.len = 1.0f;
        cfg.thick = 0.038f * scale; // wisp ribbon thickness
        cfg.trailLength = 40;       // history count
        cfg.life = 999.0f;          // infinite until detached
        cfg.tex = wispTex;
        cfg.forceField = &s_wispForceField;
        cfg.gradient = NULL;         // use default wisp style taper based on tint
        cfg.blendMode = BLEND_ALPHA; // Alpha blend! Not additive!
        cfg.ownerTag = casterAgentId;
        cfg.priority = VFX_PRIORITY_LOW;
        cfg.tint = (Color){aura->color.r, aura->color.g, aura->color.b, 85}; // Alpha 85 (~0.33 alpha)

        int id = SpawnTrailEntity(cfg);
        aura->trailIds[s] = id;
    }
}

void VFX_DetachQiAura(int casterAgentId)
{
    for (int i = 0; i < MAX_ACTIVE_AURAS; i++)
    {
        if (s_activeAuras[i].active && s_activeAuras[i].casterAgentId == casterAgentId)
        {
            for (int s = 0; s < s_activeAuras[i].wispCount; s++)
            {
                KillTrail(s_activeAuras[i].trailIds[s]);
            }
            s_activeAuras[i].active = false;
            return;
        }
    }
}

void VFX_UpdateQiAuras(float dt)
{
    float time = (float)GetTime();
    for (int i = 0; i < MAX_ACTIVE_AURAS; i++)
    {
        ActiveQiAura *aura = &s_activeAuras[i];
        if (!aura->active)
            continue;

        Vector3 pos = aura->staticPos;
        if (aura->casterAgentId >= 0)
        {
            SkillManager_GetAgentPos(aura->casterAgentId, &pos);
        }

        aura->time += dt;

        float orbitRadius = 0.65f * aura->scale;
        float orbitSpeed = 1.6f;

        for (int s = 0; s < aura->wispCount; s++)
        {
            float orbitHeight = 0.05f + ((float)s / aura->wispCount) * 0.9f * aura->bodyHeight;
            float orbitPhase = ((float)s / aura->wispCount) * 2.0f * PI + aura->time * orbitSpeed;

            Vector3 orbitOffset = {
                cosf(orbitPhase) * orbitRadius,
                orbitHeight,
                sinf(orbitPhase) * orbitRadius};

            Vector3 tipPos = Vector3Add(pos, orbitOffset);
            UpdateFollowerPosition(aura->trailIds[s], tipPos);

            // Update axis for FORCE_VORTEX_AXIS
            SetFollowerAxis(aura->trailIds[s], pos, (Vector3){0.0f, 1.0f, 0.0f});

            // 3. Sparkles (Lớp 3) - 25% * dt chance per frame
            if (((float)rand() / (float)RAND_MAX) < 0.25f * dt)
            {
                VFXLight_Spawn(tipPos, aura->color, 0.35f * aura->scale, 0.15f, VFX_PRIORITY_LOW);

                float speedX = ((float)rand() / (float)RAND_MAX - 0.5f) * 0.25f;
                float speedY = ((float)rand() / (float)RAND_MAX - 0.5f) * 0.25f;
                float speedZ = ((float)rand() / (float)RAND_MAX - 0.5f) * 0.25f;

                ParticleConfig spark = {0};
                spark.position = tipPos;
                spark.velocity = (Vector3){speedX, speedY, speedZ};
                spark.radius = 0.04f * aura->scale;
                spark.lifetime = 0.15f;
                spark.colorStart = (Color){255, 255, 255, 255};
                spark.colorEnd = (Color){aura->color.r, aura->color.g, aura->color.b, 0};
                SpawnParticle(spark);
            }
        }
    }
}
