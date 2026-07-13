#define ICE_CRYSTAL_BURST_MAX 32 

void VFX_ComposeIceCrystal(Vector3 basePos, int seed)
{
    CrystalMaterial iceMat = GetIceCrystalMaterial();
    CrystalDesc desc = GetIceCrystalDesc();

    CrystalMaterial_Begin(iceMat);
    ProceduralMesh_DrawCrystalCluster(basePos, &desc, 3, seed, 1.0f, WHITE);
    CrystalMaterial_End();
}

void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress)
{
    Mesh templateMesh = GetIceCrystalTemplateMesh();
    if (templateMesh.vertexCount <= 0)
        return;
    if (crystalCount > ICE_CRYSTAL_BURST_MAX)
        crystalCount = ICE_CRYSTAL_BURST_MAX;
    if (crystalCount <= 0)
        return;

    CrystalDesc desc = GetIceCrystalDesc();
    Matrix transforms[ICE_CRYSTAL_BURST_MAX];

    unsigned int rng = (unsigned int)seed * 747796405u + 2891336453u;
    for (int i = 0; i < crystalCount; i++)
    {
        rng = rng * 1664525u + 1013904223u;
        float r01 = (float)(rng >> 8 & 0xFFFF) / 65535.0f; 
        rng = rng * 1664525u + 1013904223u;
        float r02 = (float)(rng >> 8 & 0xFFFF) / 65535.0f; 
        rng = rng * 1664525u + 1013904223u;
        float r03 = (float)(rng >> 8 & 0xFFFF) / 65535.0f; 
        rng = rng * 1664525u + 1013904223u;
        float r04 = (float)(rng >> 8 & 0xFFFF) / 65535.0f; 
        rng = rng * 1664525u + 1013904223u;
        float r05 = (float)(rng >> 8 & 0xFFFF) / 65535.0f; 

        float angle = r01 * 2.0f * PI;
        float dist = r02 * desc.radius * 0.9f;
        float yOffset = -desc.height * 0.15f * r03;
        Vector3 pos = {
            center.x + cosf(angle) * dist,
            center.y + yOffset,
            center.z + sinf(angle) * dist};

        float heightScale = 0.4f + r04 * 0.8f; 
        float radiusScale = 0.4f + r02 * 0.6f; 
        float tiltRad = (r05 * 45.0f) * DEG2RAD;

        Matrix scale = MatrixScale(radiusScale, heightScale, radiusScale);
        Matrix rot = MatrixMultiply(MatrixMultiply(MatrixRotateY(-angle), MatrixRotateZ(tiltRad)), MatrixRotateY(angle));
        transforms[i] = MatrixMultiply(MatrixMultiply(scale, rot), MatrixTranslate(pos.x, pos.y, pos.z));
    }

    CrystalMaterialInstanced iceMatI = GetIceCrystalMaterialInstanced();
    CrystalMaterialInstanced_Begin(iceMatI);
    CrystalMaterialInstanced_SetGrowProgress(iceMatI, growProgress);
    Material passthrough = ProceduralMesh_GetPassthroughMaterial(iceMatI.shader);
    DrawMeshInstanced(templateMesh, passthrough, transforms, crystalCount);
    CrystalMaterialInstanced_End();
}
