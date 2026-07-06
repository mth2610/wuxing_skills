#include "core/presets/vfx_presets.h"

void VFX_TriggerExplosion(ExplosionStyle style, Vector3 pos, float scale, bool cameraShake)
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

    // Per-style: material nguyên tố + decal + tầm sáng. Gradient/force field
    // lấy thẳng từ material (POISON/HOLY/VOID giờ có gradient bản sắc riêng
    // thay vì mượn wood/taiji như trước).
    const VFX_ElementMaterial *mat;
    const char *decalPath;
    switch (style)
    {
        case EXP_FIRE:
            mat = VFX_Material(VC_MAT_FIRE);
            decalPath = "assets/textures/decals/decal_burn.png";
            config.lightColor = mat->glow; config.lightRadius = 3.5f;
            break;
        case EXP_ICE:
            mat = VFX_Material(VC_MAT_ICE);
            decalPath = "assets/textures/decals/decal_crack.png";
            config.lightColor = mat->body; config.lightRadius = 3.0f;
            break;
        case EXP_LIGHTNING:
            mat = VFX_Material(VC_MAT_LIGHTNING);
            decalPath = "assets/textures/decals/decal_crack.png";
            config.lightColor = mat->glow; config.lightRadius = 4.0f;
            break;
        case EXP_EARTH:
            mat = VFX_Material(VC_MAT_EARTH);
            decalPath = "assets/textures/decals/decal_crack.png";
            config.lightColor = mat->glow; config.lightRadius = 3.0f;
            break;
        case EXP_POISON:
            mat = VFX_Material(VC_MAT_POISON);
            decalPath = "assets/textures/decals/decal_burn.png";
            config.lightColor = mat->body; config.lightRadius = 3.2f;
            break;
        case EXP_HOLY:
            mat = VFX_Material(VC_MAT_HOLY);
            decalPath = "assets/textures/decals/decal_burn.png";
            config.lightColor = mat->glow; config.lightRadius = 4.5f;
            break;
        case EXP_VOID:
        default:
            mat = VFX_Material(VC_MAT_VOID);
            decalPath = "assets/textures/decals/decal_burn.png";
            config.lightColor = mat->glow; config.lightRadius = 3.8f;
            break;
    }
    config.decalTex = ResourceManager_LoadTexture(decalPath);
    p->gradient = mat->grad;
    p->forceField = (ForceField *)mat->fld;

    VFX_ComposeTriggerImpactBurst(pos, scale, &config);

    if (cameraShake)
    {
        CameraFX_Shake(0.35f * scale);
    }
}
