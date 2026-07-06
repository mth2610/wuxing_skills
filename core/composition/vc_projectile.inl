#include "core/presets/vfx_presets.h"

void VFX_ComposeProjectile(ProjectileStyle style, Vector3 pos, Vector3 target, float progress, float scale, float time)
{
    // Point light flash on the projectile
    Color lightCol = WHITE;
    float lightRadius = 1.0f * scale;

    switch (style)
    {
        case PROJECTILE_FIREBALL:
        {
            // Core sphere (additive yellow/white)
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthMask();
            EffectMaterialParams coreParams = {0};
            coreParams.baseColor = (Color){255, 200, 110, 255};
            coreParams.emissiveIntensity = 3.0f;
            EffectMaterial coreMat = Material_LoadCustom(coreParams);
            Material_Begin(coreMat);
            DrawCoreSphere(pos, 0.12f * scale, 12, 12, WHITE);
            Material_End();

            // Aura sphere
            EffectMaterialParams auraParams = {0};
            auraParams.baseColor = (Color){220, 60, 5, 120};
            auraParams.rimStrength = 2.0f;
            auraParams.fresnelPower = 2.0f;
            auraParams.emissiveIntensity = 1.5f;
            EffectMaterial auraMat = Material_LoadCustom(auraParams);
            Material_Begin(auraMat);
            DrawCoreSphere(pos, 0.22f * scale, 12, 12, WHITE);
            Material_End();
            rlEnableDepthMask();
            EndBlendMode();

            // Spawn fire particles
            if (GetRandomValue(0, 100) < 40)
            {
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f, ((float)rand() / (float)RAND_MAX) * 0.15f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f},
                    .radius = (0.04f + ((float)rand() / (float)RAND_MAX) * 0.05f) * scale,
                    .lifetime = 0.4f + ((float)rand() / (float)RAND_MAX) * 0.3f,
                    .gradient = &s_fireGrad,
                    .forceField = &s_fireFld
                });
            }

            lightCol = VFX_Material(VC_MAT_FIRE)->glow;
            lightRadius = 2.0f * scale;
            break;
        }
        case PROJECTILE_ICE:
        {
            // Rotating Ice shard
            rlDisableBackfaceCulling();
            EffectMaterial mat = Material_Get(MAT_ICE);
            BeginBlendMode(BLEND_ALPHA);
            Material_Begin(mat);
            rlPushMatrix();
            rlTranslatef(pos.x, pos.y, pos.z);
            rlRotatef(time * 350.0f, 0.0f, 1.0f, 0.0f);
            rlRotatef(time * 120.0f, 1.0f, 0.0f, 0.0f);
            rlScalef(0.18f * scale, 0.18f * scale, 0.18f * scale);
            DrawCoreSphere((Vector3){0, 0, 0}, 1.0f, 8, 8, WHITE);
            rlPopMatrix();
            Material_End();
            EndBlendMode();
            rlEnableBackfaceCulling();

            // Spawn frost particles
            if (GetRandomValue(0, 100) < 30)
            {
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = (Vector3){0, 0.05f, 0},
                    .radius = (0.03f + ((float)rand() / (float)RAND_MAX) * 0.04f) * scale,
                    .lifetime = 0.5f,
                    .gradient = &s_snowGrad,
                    .forceField = &s_snowFld
                });
            }

            lightCol = (Color){100, 200, 255, 255};
            lightRadius = 1.5f * scale;
            break;
        }
        case PROJECTILE_LIGHTNING:
        {
            // Draw a mini electric procedural bolt centered at projectile
            Vector3 sparkEnd = Vector3Add(pos, (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 0.3f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.3f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.3f});
            SpawnProcBolt(ProcRay_BoltLightningConfig(), 0.2f * scale);

            // Sparks
            if (GetRandomValue(0, 100) < 60)
            {
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f},
                    .radius = (0.015f + ((float)rand() / (float)RAND_MAX) * 0.02f) * scale,
                    .lifetime = 0.25f,
                    .gradient = &s_lightningGrad,
                    .forceField = &s_lightningFld
                });
            }

            lightCol = (Color){120, 220, 255, 255};
            lightRadius = 2.2f * scale;
            break;
        }
        case PROJECTILE_WOOD_SEED:
        {
            // Organic glowing green seed core
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthMask();
            EffectMaterialParams seedParams = {0};
            seedParams.baseColor = ColorAlpha(ELEMENT_COLOR_WOOD, 0.8f);
            seedParams.emissiveIntensity = 2.5f;
            EffectMaterial seedMat = Material_LoadCustom(seedParams);
            Material_Begin(seedMat);
            DrawCoreSphere(pos, 0.14f * scale, 8, 8, WHITE);
            Material_End();
            rlEnableDepthMask();
            EndBlendMode();

            // Leaf trailing particles
            if (GetRandomValue(0, 100) < 25)
            {
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f, -0.05f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f},
                    .radius = (0.02f + ((float)rand() / (float)RAND_MAX) * 0.03f) * scale,
                    .lifetime = 0.6f,
                    .gradient = &s_woodGrad,
                    .forceField = &s_woodFld
                });
            }

            lightCol = VFX_Material(VC_MAT_WOOD)->body;
            lightRadius = 1.2f * scale;
            break;
        }
        case PROJECTILE_ROCK:
        {
            // Rotating rock mesh
            float randScale = 0.8f + ((float)rand() / (float)RAND_MAX * 0.4f);
            EffectMaterial rockMat = Material_Get(MAT_ROCK);
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            Material_Begin(rockMat);
            rlPushMatrix();
            rlTranslatef(pos.x, pos.y, pos.z);
            rlRotatef(time * 200.0f, 0.0f, 1.0f, 0.0f);
            rlScalef(0.12f * scale * randScale, 0.12f * scale * randScale, 0.12f * scale * randScale);
            DrawCoreSphere((Vector3){0, 0, 0}, 1.0f, 8, 8, WHITE);
            rlPopMatrix();
            Material_End();
            rlEnableBackfaceCulling();

            // Dust trail
            if (GetRandomValue(0, 100) < 40)
            {
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 0.1f, -0.02f, ((float)rand() / (float)RAND_MAX - 0.5f) * 0.1f},
                    .radius = (0.03f + ((float)rand() / (float)RAND_MAX) * 0.04f) * scale,
                    .lifetime = 0.5f,
                    .gradient = &s_earthGrad,
                    .forceField = &s_earthFld
                });
            }

            lightCol = (Color){200, 160, 100, 255};
            lightRadius = 1.0f * scale;
            break;
        }
        case PROJECTILE_YINYANG:
        {
            // Orbiting black and white motes
            float r_orbit = 0.18f * scale;
            Vector3 mote1 = VC_MotionOrbit(pos, r_orbit, 12.0f, time, 0.0f);
            Vector3 mote2 = VC_MotionOrbit(pos, r_orbit, 12.0f, time, PI);

            // Render motes
            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            DrawCoreSphere(mote1, 0.05f * scale, 8, 8, WHITE);
            DrawCoreSphere(mote2, 0.05f * scale, 8, 8, BLACK);
            rlEnableDepthMask();
            EndBlendMode();

            // Mote particles
            if (GetRandomValue(0, 100) < 30)
            {
                SpawnParticle((ParticleConfig){
                    .position = mote1,
                    .velocity = (Vector3){0, 0.02f, 0},
                    .radius = 0.02f * scale,
                    .lifetime = 0.3f,
                    .gradient = &s_taijiGrad,
                    .forceField = &s_taijiFld
                });
                SpawnParticle((ParticleConfig){
                    .position = mote2,
                    .velocity = (Vector3){0, 0.02f, 0},
                    .radius = 0.02f * scale,
                    .lifetime = 0.3f,
                    .gradient = &s_taijiGrad,
                    .forceField = &s_taijiFld
                });
            }

            lightCol = (Color){200, 200, 200, 255};
            lightRadius = 1.8f * scale;
            break;
        }
    }

    // Spawn a temporary low-priority light at the projectile position to make it glow in the environment
    VFXLight_Spawn(pos, lightCol, lightRadius, 0.05f, VFX_PRIORITY_LOW);
}
