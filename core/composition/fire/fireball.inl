void VFX_ComposeFireball(Vector3 pos, float time)
{
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();

    float radius = 0.16f;
    float breathe = 0.03f * sinf(time * 6.0f);
    Vector3 actualPos = Vector3Add(pos, (Vector3){0, 0.25f + breathe, 0});

    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){255, 210, 130, 255};
    coreParams.emissiveIntensity = 2.5f;
    EffectMaterial coreMat;
    Material_LoadCustom(&coreMat, &coreParams);
    Material_Begin(coreMat);
    DrawCoreSphere(actualPos, (radius * 0.55f), 16, 16, WHITE);
    Material_End();

    EffectMaterialParams auraParams = {0};
    auraParams.baseColor = (Color){240, 80, 10, 160};
    auraParams.rimStrength = 2.2f;
    auraParams.fresnelPower = 2.5f;
    auraParams.emissiveIntensity = 1.2f;
    auraParams.distortionStrength = 0.45f;
    auraParams.translucency = 0.6f;
    EffectMaterial auraMat;
    Material_LoadCustom(&auraMat, &auraParams);
    Material_Begin(auraMat);
    DrawCoreSphere(actualPos, radius, 16, 16, WHITE);
    Material_End();
    rlEnableDepthMask();
    EndBlendMode();
}
