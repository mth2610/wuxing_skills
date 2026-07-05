void VFX_ComposeStonePillar(Vector3 basePos, float progress)
{
    if (progress <= 0.0f)
        return;

    rlDisableBackfaceCulling();
    float height = 1.8f;
    float radius = 0.35f;

    float rise = progress * progress * (3.0f - 2.0f * progress);
    float yOffset = -height * (1.0f - rise) - 0.05f;
    Vector3 actualPos = Vector3Add(basePos, (Vector3){0, yOffset, 0});
    float topRadius = radius * 0.55f;

    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    ProceduralMesh_DrawOrganicStonePillar(actualPos, height + 0.05f, radius, topRadius);
    Material_End();
}

void VFX_ComposeBoulder(Vector3 pos)
{
    float randScale = 0.88f + ((float)rand() / (float)RAND_MAX * 0.24f);

    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    DrawCoreSphere(pos, 0.22f * randScale, 24, 24, WHITE);
    Material_End();

    RockMeshData *data = MeshCache_GetRock(rand() % 5000, 0.45f);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Material_Begin(mat);

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);

    rlRotatef((float)(rand() % 360), 0.0f, 1.0f, 0.0f);
    rlRotatef((float)(rand() % 20 - 10), 1.0f, 0.0f, 0.0f);

    rlScalef(0.25f * randScale, 0.25f * randScale, 0.25f * randScale);
    ProceduralMesh_DrawRock(data, WHITE);
    rlPopMatrix();

    Material_End();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width)
{
    float totalDist = Vector3Distance(start, end);
    if (totalDist > 0.001f)
    {
        Vector3 dir = Vector3Normalize(Vector3Subtract(end, start));
        Vector3 right = (Vector3){-dir.z, 0.0f, dir.x};
        float halfW = width * 0.5f;

        Vector3 p1 = Vector3Add(start, Vector3Scale(right, -halfW));
        Vector3 p2 = Vector3Add(end, Vector3Scale(right, -halfW));
        Vector3 p3 = Vector3Add(end, Vector3Scale(right, halfW));
        Vector3 p4 = Vector3Add(start, Vector3Scale(right, halfW));

        float yOffset = 0.006f;
        p1.y += yOffset;
        p2.y += yOffset;
        p3.y += yOffset;
        p4.y += yOffset;

        Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");

        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ALPHA);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(p3.x, p3.y, p3.z);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(p4.x, p4.y, p4.z);
        rlEnd();
        rlSetTexture(0);

        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }
}
