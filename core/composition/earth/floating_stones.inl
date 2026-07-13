#define FLOATING_STONE_MAX 8 

void VFX_ComposeFloatingStones(Vector3 pos, float radius, float time)
{
    EarthFx_InitShared();

    Mesh templateMesh = GetFloatingStoneTemplateMesh();
    const int stoneCount = 5;
    Vector3 stonePos[5];
    Matrix transforms[FLOATING_STONE_MAX];

    if (templateMesh.vertexCount > 0)
    {
        for (int i = 0; i < stoneCount; i++)
        {
            unsigned int h = (unsigned int)i * 2246822519u + 3266489917u;
            float r01 = (float)(h >> 8 & 0xFFFF) / 65535.0f;
            float r02 = (float)(h >> 20 & 0xFFF) / 4095.0f;

            float a = time * (0.5f + 0.25f * r01) * ((i % 2) ? 1.0f : -1.0f) + (float)i * (2.0f * PI / (float)stoneCount);
            float orbR = radius * (0.75f + 0.4f * r01);
            float y = radius * (0.5f + 0.55f * r02) + radius * 0.1f * sinf(time * 1.3f + (float)i * 2.1f);
            Vector3 p = {pos.x + cosf(a) * orbR, pos.y + y, pos.z + sinf(a) * orbR};
            stonePos[i] = p;

            float s = radius * (0.10f + 0.08f * r02);
            float tumbleRad = (time * (8.0f + 10.0f * r01)) * DEG2RAD; 
            Matrix scale = MatrixScale(s, s, s);
            Matrix rot = MatrixRotate((Vector3){r01, 1.0f, r02}, tumbleRad);
            Matrix translate = MatrixTranslate(p.x, p.y, p.z);
            transforms[i] = MatrixMultiply(MatrixMultiply(scale, rot), translate);
        }

        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        EffectMaterialInstanced rockMatI = GetFloatingStoneMaterialInstanced();
        EffectMaterialInstanced_Begin(rockMatI);
        Material passthrough = ProceduralMesh_GetPassthroughMaterial(rockMatI.shader);
        DrawMeshInstanced(templateMesh, passthrough, transforms, stoneCount);
        EffectMaterialInstanced_End();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
    else
    {
        for (int i = 0; i < stoneCount; i++) stonePos[i] = pos;
    }

    if (GetRandomValue(0, 100) < 40)
    {
        int i = GetRandomValue(0, stoneCount - 1);
        SpawnParticle((ParticleConfig){
            .position = (Vector3){stonePos[i].x + (Random01() - 0.5f) * 0.08f,
                                  pos.y + 0.03f,
                                  stonePos[i].z + (Random01() - 0.5f) * 0.08f},
            .velocity = (Vector3){0, 0.25f + Random01() * 0.2f, 0},
            .radius = 0.006f + Random01() * 0.005f,
            .lifetime = 0.6f + Random01() * 0.4f,
            .gradient = &s_earthGrainGrad});
    }

    if (GetRandomValue(0, 100) < 8)
    {
        int i = GetRandomValue(0, stoneCount - 1);
        SpawnParticle((ParticleConfig){
            .position = stonePos[i],
            .velocity = (Vector3){(Random01() - 0.5f) * 0.2f, -0.1f, (Random01() - 0.5f) * 0.2f},
            .radius = 0.007f + Random01() * 0.004f,
            .lifetime = 0.6f,
            .gradient = &s_earthChunkGrad,
            .forceField = EarthGravField()});
    }

    if (GetRandomValue(0, 100) < 10)
        VFXLight_Spawn(Vector3Add(pos, (Vector3){0, radius * 0.6f, 0}),
                       VFX_Material(VC_MAT_EARTH)->soft, radius * 1.8f, 0.25f, VFX_PRIORITY_LOW);
}
