#include "core/presets/vfx_presets.h"

void VFX_TriggerExplosion(VC_MaterialId matId, Vector3 pos, float scale, bool cameraShake)
{
    ImpactBurstConfig config = {0};
    config.distortEnabled = true;
    config.distortRadius = 2.0f * scale;
    config.distortStrength = 0.3f;
    config.distortLife = 0.4f;
    config.distortSpeed = 4.5f;

    config.decalEnabled = true;
    config.decalScale = 1.0f;
    config.decalLife = 3.0f;
    config.decalTint = WHITE;
    config.decalRandomRotation = true;

    config.lightEnabled = true;
    config.lightLife = 0.35f;

    config.particlesEnabled = true;

    // Config particle burst presets
    ParticleRadialBurstConfig *p = &config.particles;
    p->countMin = 25;
    p->countMax = 40;
    p->speedMin = 0.5f;
    p->speedMax = 1.8f;
    p->radiusMin = 0.1f;
    p->radiusMax = 0.4f;
    p->lifetimeMin = 0.4f;
    p->lifetimeMax = 0.8f;
    p->pitchRange = PI * 0.8f;
    p->upwardBias = 0.3f;

    // Material nguyên tố cấp màu sáng + gradient/force field cho hạt nổ.
    // Decal: các hệ "vỡ/giòn" để lại vết nứt, các hệ "cháy/năng lượng" để lại vết cháy.
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    bool crackDecal = (matId == VC_MAT_ICE || matId == VC_MAT_LIGHTNING ||
                       matId == VC_MAT_EARTH || matId == VC_MAT_METAL);
    config.decalTex = ResourceManager_LoadTexture(crackDecal ? "assets/textures/decals/decal_crack.png"
                                                             : "assets/textures/decals/decal_burn.png");
    config.lightColor = mat->glow;

    // Tầm sáng: giữ accent cũ per-element (sét/thánh quang nổ chói hơn đất/băng).
    float lightR = 3.5f;
    if (matId == VC_MAT_LIGHTNING)   lightR = 4.0f;
    else if (matId == VC_MAT_HOLY)   lightR = 4.5f;
    else if (matId == VC_MAT_VOID)   lightR = 3.8f;
    else if (matId == VC_MAT_POISON) lightR = 3.2f;
    else if (matId == VC_MAT_ICE || matId == VC_MAT_EARTH) lightR = 3.0f;
    config.lightRadius = lightR;

    p->gradient = mat->grad;
    p->forceField = (ForceField *)mat->fld;

    VFX_ComposeTriggerImpactBurst(pos, scale, &config);

    if (cameraShake)
    {
        CameraFX_Shake(0.35f * scale);
    }
}
