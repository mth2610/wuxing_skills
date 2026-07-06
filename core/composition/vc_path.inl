void VFX_PathWave(PathStyle style, const Vector3 *points, int count, float scale, float progress, float time)
{
    if (points == NULL || count < 2) return;

    for (int i = 0; i < count; i++)
    {
        float pointProgress = (float)i / (float)(count - 1);
        if (progress < pointProgress) continue;

        // How long ago the wave front passed this point
        float localAge = progress - pointProgress;
        float localT = localAge / 0.25f; // reaches max height in 0.25s
        if (localT > 1.0f) localT = 1.0f;

        Vector3 pos = points[i];

        switch (style)
        {
            case PATH_STONE_PILLAR:
            {
                // Draw a stone pillar rising at this point
                rlPushMatrix();
                rlTranslatef(pos.x, pos.y, pos.z);
                rlScalef(scale, scale, scale);
                VFX_ComposeStonePillar((Vector3){0, 0, 0}, localT);
                rlPopMatrix();

                // Earth puff when emerging
                if (localT < 0.2f && GetRandomValue(0, 10) < 3)
                {
                    VFX_ComposeSmokePuff(pos, 0.8f * scale);
                }
                break;
            }
            case PATH_ICE_SPIKE:
            {
                // Draw ice crystals rising
                if (localT > 0.0f)
                {
                    rlPushMatrix();
                    rlTranslatef(pos.x, pos.y, pos.z);
                    rlScalef(scale * localT, scale * localT, scale * localT);
                    VFX_ComposeIceCrystal((Vector3){0, 0, 0}, i);
                    rlPopMatrix();
                }
                break;
            }
            case PATH_THORNS:
            {
                // Wood vine loop
                if (localT > 0.0f)
                {
                    BeginBlendMode(BLEND_ALPHA);
                    EffectMaterialParams matParams = {0};
                    matParams.baseColor = ColorAlpha(ELEMENT_COLOR_WOOD, localT);
                    matParams.rimStrength = 2.0f;
                    matParams.fresnelPower = 2.0f;
                    matParams.emissiveIntensity = 1.5f;
                    EffectMaterial mat = Material_LoadCustom(matParams);
                    Material_Begin(mat);
                    rlPushMatrix();
                    rlTranslatef(pos.x, pos.y + (localT * 0.4f * scale) - 0.2f, pos.z);
                    rlScalef(0.1f * scale, 0.3f * scale * localT, 0.1f * scale);
                    DrawCoreSphere((Vector3){0, 0, 0}, 1.0f, 8, 8, WHITE);
                    rlPopMatrix();
                    Material_End();
                    EndBlendMode();
                }
                break;
            }
            case PATH_FIRE_ERUPTION:
            {
                // Flame spout rising
                if (localT > 0.0f && localT < 0.9f)
                {
                    float fireHeight = localT * 1.5f * scale;
                    VFXLight_Spawn(pos, VFX_Material(VC_MAT_FIRE)->soft, 1.5f * scale, 0.1f, VFX_PRIORITY_LOW);

                    if (GetRandomValue(0, 100) < 30)
                    {
                        SpawnParticle((ParticleConfig){
                            .position = (Vector3){pos.x, pos.y + fireHeight * 0.5f, pos.z},
                            .velocity = (Vector3){0, 1.2f, 0},
                            .radius = 0.15f * scale,
                            .lifetime = 0.4f,
                            .gradient = &s_fireGrad,
                            .forceField = &s_fireFld
                        });
                    }
                }
                break;
            }
            case PATH_LIGHTNING_CHAIN:
            {
                // Draw dynamic lightning chain between segments
                if (i < count - 1)
                {
                    float nextPointProgress = (float)(i + 1) / (float)(count - 1);
                    if (progress >= nextPointProgress)
                    {
                        if (GetRandomValue(0, 100) < 20)
                        {
                            VFX_ComposeLightningBolt(pos, points[i + 1], 0.4f * scale);
                        }
                    }
                }
                break;
            }
        }
    }
}
