void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time)
{
    float totalDist = Vector3Distance(start, end);
    if (totalDist < 0.05f)
        return;
    progress = Clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f)
        return;

    int seed = (int)(start.x * 131.0f) + (int)(start.z * 977.0f) +
               (int)(end.x * 53.0f) + (int)(end.z * 197.0f);

    Vector3 liftedStart = Vector3Add(start, (Vector3){0.0f, 0.02f, 0.0f});
    Vector3 liftedEnd   = Vector3Add(end,   (Vector3){0.0f, 0.02f, 0.0f});
    Vector3 path[2] = {liftedStart, liftedEnd};
    static FissureMeshData mesh;
    ProceduralMesh_BuildFissure(&mesh, path, 2, width, 0.12f, 0.75f, seed);
    if (mesh.segments < 1)
        return;

    int revealSeg = (int)(mesh.segments * progress + 0.999f);
    if (revealSeg < 1)
        revealSeg = 1;
    if (revealSeg > mesh.segments)
        revealSeg = mesh.segments;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthTest();
    Color crossColors[FISSURE_CROSS_VERTS] = {
        (Color){95, 78, 58, 255},  
        (Color){52, 42, 32, 255},  
        (Color){14, 10, 8, 255},   
        (Color){52, 42, 32, 255},  
        (Color){95, 78, 58, 255},  
    };
    ProceduralMesh_DrawFissureShaded(&mesh, crossColors, revealSeg);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableBackfaceCulling();

    float pulse = 0.55f + 0.45f * sinf(time * 3.2f);
    unsigned char glowA = (unsigned char)(90.0f * pulse);
    float glowHW = width * 0.55f;

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlBegin(RL_QUADS);
    rlColor4ub(255, 140, 70, glowA);
    for (int i = 0; i < revealSeg; i++)
    {
        Vector3 acrossA = Vector3Normalize(Vector3Subtract(mesh.verts[i][3], mesh.verts[i][1]));
        Vector3 acrossB = Vector3Normalize(Vector3Subtract(mesh.verts[i + 1][3], mesh.verts[i + 1][1]));
        Vector3 a0 = Vector3Add(mesh.verts[i][2], Vector3Scale(acrossA, -glowHW));
        Vector3 a1 = Vector3Add(mesh.verts[i][2], Vector3Scale(acrossA, glowHW));
        Vector3 b1 = Vector3Add(mesh.verts[i + 1][2], Vector3Scale(acrossB, glowHW));
        Vector3 b0 = Vector3Add(mesh.verts[i + 1][2], Vector3Scale(acrossB, -glowHW));
        a0.y += 0.008f;
        a1.y += 0.008f;
        b0.y += 0.008f;
        b1.y += 0.008f;
        rlVertex3f(a0.x, a0.y, a0.z);
        rlVertex3f(a1.x, a1.y, a1.z);
        rlVertex3f(b1.x, b1.y, b1.z);
        rlVertex3f(b0.x, b0.y, b0.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    if (progress < 1.0f && GetRandomValue(0, 100) < 30)
    {
        EarthFx_InitShared();
        Vector3 tipDir = Vector3Normalize(Vector3Subtract(end, start));
        Vector3 tip = Vector3Add(start, Vector3Scale(tipDir, totalDist * progress));
        SpawnParticle((ParticleConfig){
            .position = (Vector3){tip.x, tip.y + 0.03f, tip.z},
            .velocity = (Vector3){(Random01() - 0.5f) * 0.3f, 0.3f + Random01() * 0.3f, (Random01() - 0.5f) * 0.3f},
            .radius = 0.03f + Random01() * 0.02f,
            .lifetime = 0.4f + Random01() * 0.3f,
            .gradient = &s_earthDustGrad,
            .radiusCurve = &s_earthDustBillow});
    }
}
