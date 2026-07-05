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

    switch (style)
    {
        case EXP_FIRE:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            config.lightColor = (Color){255, 120, 10, 255};
            config.lightRadius = 3.5f;
            p->gradient = &s_fireGrad;
            p->forceField = &s_fireFld;
            break;
        }
        case EXP_ICE:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            config.lightColor = (Color){150, 220, 255, 255};
            config.lightRadius = 3.0f;
            p->gradient = &s_snowGrad;
            p->forceField = &s_snowFld;
            break;
        }
        case EXP_LIGHTNING:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            config.lightColor = (Color){0, 180, 255, 255};
            config.lightRadius = 4.0f;
            p->gradient = &s_lightningGrad;
            p->forceField = &s_lightningFld;
            break;
        }
        case EXP_EARTH:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            config.lightColor = (Color){220, 160, 100, 255};
            config.lightRadius = 3.0f;
            p->gradient = &s_earthGrad;
            p->forceField = &s_earthFld;
            break;
        }
        case EXP_POISON:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            config.lightColor = (Color){80, 255, 100, 255};
            config.lightRadius = 3.2f;
            p->gradient = &s_woodGrad; // green tint
            p->forceField = &s_woodFld;
            break;
        }
        case EXP_HOLY:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            config.lightColor = (Color){255, 235, 150, 255};
            config.lightRadius = 4.5f;
            p->gradient = &s_taijiGrad; // golden/yinyang tint
            p->forceField = &s_taijiFld;
            break;
        }
        case EXP_VOID:
        {
            config.decalTex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            config.lightColor = (Color){160, 30, 220, 255};
            config.lightRadius = 3.8f;
            p->gradient = &s_taijiGrad; // purple/black tint
            p->forceField = &s_taijiFld;
            break;
        }
    }

    VFX_ComposeTriggerImpactBurst(pos, scale, &config);

    if (cameraShake)
    {
        CameraFX_Shake(0.35f * scale);
    }
}
