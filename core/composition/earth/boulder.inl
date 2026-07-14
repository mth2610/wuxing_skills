void VFX_ComposeBoulder(Vector3 pos)
{
    float randScale = 0.88f + ((float)rand() / (float)RAND_MAX * 0.24f);

    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);
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
