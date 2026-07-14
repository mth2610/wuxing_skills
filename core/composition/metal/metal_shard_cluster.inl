void VFX_ComposeMetalShardCluster(Vector3 basePos, int seed)
{
    static CrystalMaterial s_metalMat;
    static bool s_metalMatLoaded = false;
    if (!s_metalMatLoaded)
    {
        CrystalMaterialParams p = {0};
        p.baseColor = (Color){108, 120, 130, 255}; 
        p.edgeColor = (Color){245, 250, 255, 255}; 
        p.roughness = 0.82f;                       
        p.fresnel = 2.0f;                          
        p.refraction = 0.0f;                       
        p.sparkle = 1.0f;                          
        p.crack = 0.0f;                            
        p.emission = 0.06f;
        p.thickness = 0.6f;
        CrystalMaterial_Load(&s_metalMat, &p);
        s_metalMatLoaded = true;
    }

    CrystalDesc desc = GetMetalBladeDesc();
    Mesh templateMesh = GetMetalBladeTemplateMesh();

    unsigned int rng = (unsigned int)seed * 747796405u + 2891336453u;
    Vector3 tallestTip = basePos;
    float tallestH = 0.0f;
    Matrix transforms[METAL_BLADE_COUNT];
    float bladeHeights[METAL_BLADE_COUNT];

    for (int i = 0; i < METAL_BLADE_COUNT; i++)
    {
        rng = rng * 1664525u + 1013904223u;
        float r01 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r02 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        float r03 = (float)(rng >> 8 & 0xFFFF) / 65535.0f;

        float a = (float)i / (float)METAL_BLADE_COUNT * 2.0f * PI + r01 * 1.2f;
        float dist = 0.06f + r02 * 0.14f;
        Vector3 p = {basePos.x + cosf(a) * dist, basePos.y, basePos.z + sinf(a) * dist};

        float heightScale = 0.55f + r03 * 0.7f; 
        float radiusScale = 0.75f + r01 * 0.5f;
        bladeHeights[i] = desc.height * heightScale;

        Matrix scale = MatrixScale(radiusScale, heightScale, radiusScale);
        Matrix rotFacing = MatrixRotateY(r02 * 360.0f * DEG2RAD);        
        Matrix rotLean = MatrixRotateX((r01 - 0.5f) * 24.0f * DEG2RAD);  
        Matrix rotTilt = MatrixRotateZ((r03 - 0.5f) * 24.0f * DEG2RAD);
        Matrix rot = MatrixMultiply(MatrixMultiply(rotFacing, rotLean), rotTilt);
        Matrix translate = MatrixTranslate(p.x, p.y, p.z);
        transforms[i] = MatrixMultiply(MatrixMultiply(scale, rot), translate);

        if (bladeHeights[i] > tallestH)
        {
            tallestH = bladeHeights[i];
            tallestTip = (Vector3){p.x, p.y + bladeHeights[i], p.z};
        }
    }

    if (templateMesh.vertexCount > 0)
    {
        CrystalMaterialInstanced metalMatI = GetMetalBladeMaterialInstanced();
        CrystalMaterialInstanced_Begin(metalMatI);
        Material passthrough = ProceduralMesh_GetPassthroughMaterial(metalMatI.shader);
        DrawMeshInstanced(templateMesh, passthrough, transforms, METAL_BLADE_COUNT);
        CrystalMaterialInstanced_End();
    }

    CrystalMaterial_Begin(s_metalMat);

    CrystalDesc micro = desc;
    micro.height = 0.18f;
    micro.radius = 0.045f;
    micro.noise = 0.06f;
    ProceduralMesh_DrawCrystalCluster(basePos, &micro, 5, seed * 7 + 13, 1.0f, WHITE);
    CrystalMaterial_End();

    if (GetRandomValue(0, 100) < 2)
    {
        Texture2D crackTex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
        DecalSystem_Add(basePos, (float)(seed % 360), 0.9f, crackTex, 1.2f, (Color){200, 210, 220, 200});
    }

    if (GetRandomValue(0, 100) < 7)
        VFX_ComposeGlintBurst(tallestTip, 2, 0.05f, VFX_Material(VC_MAT_METAL)->soft);
}
