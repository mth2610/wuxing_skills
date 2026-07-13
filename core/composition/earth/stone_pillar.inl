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
