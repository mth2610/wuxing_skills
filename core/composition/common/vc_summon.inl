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
