#ifndef VC_WOOD_PETALS_INL
#define VC_WOOD_PETALS_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/composition/wood/vc_wood_botanical_math.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

VFX_WoodPetalConfig VFX_WoodPetal_DefaultConfig(void)
{
    VFX_WoodPetalConfig cfg;
    cfg.origin = (Vector3){0.0f, 3.0f, 0.0f};
    cfg.radius = 1.4f;
    cfg.count = 64;
    cfg.mass = 0; // Derived from sheet material and generated area.
    cfg.bodyMaterial = BODY_LAMINA_PETAL_FRESH;
    cfg.initialVelocity = (Vector3){0.0f, 0.35f, 0.0f};
    cfg.velocitySpread = 1.1f;
    cfg.size = 0; // Resolve species blade dimensions.
    cfg.type = WOOD_FLOWER_TYPE_PLUM_BLOSSOM;
    cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
    cfg.seed = 67890;
    return cfg;
}

static const char *s_woodPetalTypeDisplayNames[WOOD_FLOWER_TYPE_COUNT] = {
    "SACRED LOTUS", "CELESTIAL ORCHID", "IRONWOOD PLUM BLOSSOM"
};

static const char *s_woodPetalStyleDisplayNames[WOOD_VINE_STYLE_COUNT] = {
    "JADE EMERALD", "BLOOD BRAMBLE", "GOLDEN AMBER", "TAICHI INK"
};

int VFX_WoodPetals_GetParams(VFX_WoodPetalConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Type", .group = "Petals", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->type, .minInt = 0, .maxInt = WOOD_FLOWER_TYPE_COUNT - 1,
            .enumNames = s_woodPetalTypeDisplayNames, .enumCount = WOOD_FLOWER_TYPE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Style", .group = "Petals", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->style, .minInt = 0, .maxInt = WOOD_VINE_STYLE_COUNT - 1,
            .enumNames = s_woodPetalStyleDisplayNames, .enumCount = WOOD_VINE_STYLE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){.name="Body material", .group="Petals",
            .type=VFX_PARAM_ENUM, .valPtr=&cfg->bodyMaterial, .minInt=0, .maxInt=2,
            .enumNames=s_botanicalMaterialNames, .enumCount=3};
    }
    return n;
}

/* Renders dedicated physical drifting flower petals in wind, gravity & air drag.
 * Petals are always free (never attached), tumbling gracefully through space. */
void VFX_ComposeWoodPetals(const VFX_WoodPetalConfig *config)
{
    if (config == NULL || !isfinite(config->size))
        return;

    // Periodically feed the simulation pool with free physical airborne petals
    static float s_petalSpawnTimer = 0.0f;
    s_petalSpawnTimer += TimeFX_RawDelta();
    int targetCount = config->count > 0 ? config->count : 64;

    if (s_petalSpawnTimer > 1.2f || VFX_FoliageSystem_GetActiveCount() < targetCount / 3)
    {
        s_petalSpawnTimer = 0.0f;
        VFX_FoliageSpawnParams sp = VFX_FoliageSpawnParams_Default();
        sp.kind = BOTANICAL_KIND_PETAL;
        sp.flowerType = config->type;
        sp.style = config->style;
        sp.origin = config->origin;
        sp.radius = config->radius > 0.1f ? config->radius : 1.4f;
        sp.count = targetCount;
        sp.attached = false;
        sp.mass = config->mass > 0 ? config->mass : 0;
        sp.bodyMaterial = config->bodyMaterial;
        sp.initialVelocity = config->initialVelocity;
        sp.velocitySpread = config->velocitySpread > 0.1f ? config->velocitySpread : 1.1f;
        sp.size = Botanical_ResolvePetalSize(config->size,config->type);
        sp.growth = 1.0f;
        sp.lifetime = 12.0f;
        sp.seed = config->seed;
        VFX_FoliageSystem_SpawnCluster(&sp);
    }

    // Also render 6 showcase drifting petals orbiting origin for immediate visual feedback
    bool isShadowPass = EnvShadow_IsCapturing();
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float time = (float)TimeFX_Elapsed();

    Color petalBase, petalMid, petalTip, cGlowRim;
    if (config->type == WOOD_FLOWER_TYPE_LOTUS)
    {
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase = (Color){185, 25, 42, 255};
            petalMid  = (Color){255, 45, 75, 255};
            petalTip  = (Color){255, 110, 140, 255};
            cGlowRim  = (Color){255, 160, 180, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            petalBase = (Color){255, 245, 190, 255};
            petalMid  = (Color){255, 195, 35, 255};
            petalTip  = (Color){255, 225, 90, 255};
            cGlowRim  = (Color){255, 245, 170, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase = (Color){215, 200, 245, 255};
            petalMid  = (Color){185, 135, 255, 255};
            petalTip  = (Color){220, 185, 255, 255};
            cGlowRim  = (Color){240, 215, 255, 255};
        } else {
            petalBase = (Color){255, 245, 250, 255};
            petalMid  = (Color){255, 85, 165, 255};
            petalTip  = (Color){255, 155, 215, 255};
            cGlowRim  = (Color){255, 195, 235, 255};
        }
    }
    else if (config->type == WOOD_FLOWER_TYPE_ORCHID)
    {
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase = (Color){245, 210, 220, 255};
            petalMid  = (Color){225, 35, 75, 255};
            petalTip  = (Color){255, 110, 145, 255};
            cGlowRim  = (Color){255, 180, 205, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            petalBase = (Color){255, 248, 220, 255};
            petalMid  = (Color){240, 175, 25, 255};
            petalTip  = (Color){255, 215, 70, 255};
            cGlowRim  = (Color){255, 245, 160, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase = (Color){230, 220, 250, 255};
            petalMid  = (Color){175, 130, 245, 255};
            petalTip  = (Color){215, 180, 255, 255};
            cGlowRim  = (Color){240, 210, 255, 255};
        } else {
            petalBase = (Color){252, 248, 255, 255};
            petalMid  = (Color){215, 95, 245, 255};
            petalTip  = (Color){245, 175, 255, 255};
            cGlowRim  = (Color){255, 215, 255, 255};
        }
    }
    else
    {
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase = (Color){255, 225, 230, 255};
            petalMid  = (Color){245, 55, 85, 255};
            petalTip  = (Color){255, 125, 155, 255};
            cGlowRim  = (Color){255, 175, 195, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            petalBase = (Color){255, 250, 195, 255};
            petalMid  = (Color){255, 205, 30, 255};
            petalTip  = (Color){255, 230, 80, 255};
            cGlowRim  = (Color){255, 248, 160, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase = (Color){235, 225, 245, 255};
            petalMid  = (Color){195, 165, 250, 255};
            petalTip  = (Color){225, 200, 255, 255};
            cGlowRim  = (Color){245, 225, 255, 255};
        } else {
            petalBase = (Color){255, 245, 248, 255};
            petalMid  = (Color){255, 105, 175, 255};
            petalTip  = (Color){255, 165, 215, 255};
            cGlowRim  = (Color){255, 205, 235, 255};
        }
    }

    BotanicalProfile prof=Botanical_DetachedPetalProfile(config->type);

    rlDisableBackfaceCulling();
    if (!isShadowPass) BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_TRIANGLES);

    float petalSz = 1.25f * Botanical_ResolvePetalSize(config->size,config->type);
    for (int k = 0; k < 6; k++)
    {
        float phaseK = (float)k * 1.04719755f;
        float ang = phaseK + time * 0.85f;
        float r = 0.35f + 0.15f * sinf(time * 1.2f + phaseK);
        float y = sinf(time * 2.1f + phaseK * 2.0f) * 0.22f;

        Vector3 pos = Vector3Add(config->origin, (Vector3){ cosf(ang) * r, y, sinf(ang) * r });
        float pitch = sinf(time * 2.8f + phaseK) * 0.65f;
        float yaw   = ang + PI * 0.5f;
        float roll  = cosf(time * 3.2f + phaseK) * 0.45f;

        float cy = cosf(yaw), sy = sinf(yaw);
        float cp = cosf(pitch), sp = sinf(pitch);
        float cr = cosf(roll), sr = sinf(roll);

        Vector3 forward = Vector3Normalize((Vector3){ cy * cp, sp, sy * cp });
        Vector3 up      = Vector3Normalize((Vector3){ -sy * sr - cy * sp * cr, cp * cr, cy * sr - sy * sp * cr });
        Vector3 right   = Vector3Normalize(Vector3CrossProduct(up, forward));
        if (getenv("WUXING_BOTANICAL_TRACE") && k==0 && time>=1.48f && time<=1.52f)
            TraceLog(LOG_WARNING,"BLADE PETAL shadow=%d time=%.4f L=%.6f root=(%.6f %.6f %.6f)",isShadowPass,time,petalSz,pos.x,pos.y,pos.z);

        Botanical_RenderSinglePetalMesh(
            pos, forward, right, up, petalSz, &prof,
            petalSz * 0.22f, petalBase, petalMid, petalTip, cGlowRim,
            sunDir, isShadowPass, 255
        );
    }

    rlEnd();
    if (!isShadowPass) EndBlendMode();
    rlEnableBackfaceCulling();
}

#endif // VC_WOOD_PETALS_INL
